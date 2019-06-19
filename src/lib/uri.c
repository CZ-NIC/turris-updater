/*
 * Copyright 2018-2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the turris updater.
 *
 * Updater is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * Updater is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Updater.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "uri.h"
#include "multiwrite.h"
#include "subprocess.h"
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <uriparser/Uri.h>

#define TMP_TEMPLATE_CA_CRL_FILE "/tmp/updater-ca-XXXXXX"
#define TMP_TEMPLATE_PUBKEY_FILE "/tmp/updater-pubkey-XXXXXX"
#define TMP_TEMPLATE_SIGNATURE_FILE "/tmp/updater-sig-XXXXXX"


THREAD_LOCAL enum uri_error uri_errno = 0;
THREAD_LOCAL enum uri_error uri_sub_errno = 0;
THREAD_LOCAL struct uri *uri_sub_err_uri = NULL;

static const char *error_messages[] = {
	[URI_E_INVALID_URI] = "URI has invalid format",
	[URI_E_UNKNOWN_SCHEME] = "URI contains invalid or unsupported scheme",
	[URI_E_UNFINISHED_DOWNLOAD] = "Download wasn't finished or even started",
	[URI_E_DOWNLOAD_FAILED] = "Download failed",
	[URI_E_FILE_INPUT_ERROR] = "Unable to open local file for reading",
	[URI_E_OUTPUT_OPEN_FAIL] = "Unable to open output file for writing",
	[URI_E_OUTPUT_WRITE_FAIL] = "Unable to write data to output",
	[URI_E_SIG_FAIL] = "Signature URI failure",
	[URI_E_VERIFY_FAIL] = "Signature verification failure",
	[URI_E_NONLOCAL] = "URI to be used for local resources is not local one (file or data)",
};

static const char *schemes_table[] = {
	[URI_S_HTTP] = "http",
	[URI_S_HTTPS] = "https",
	[URI_S_FILE] = "file",
	[URI_S_DATA] = "data",
	[URI_S_UNKNOWN] = "?"
};


static void list_ca_crl_collect(struct uri_local_list*);
static void list_pubkey_collect(struct uri_local_list*);

// Bup reference count
static void list_refup(struct uri_local_list *list) {
	while(list) {
		list->ref_count++;
		list = list->next;
	}
}

// Generic function to add item to list used by all following add functions
static struct uri_local_list *list_add(struct uri_local_list *list) {
	struct uri_local_list *w = malloc(sizeof *w);
	*w = (struct uri_local_list) {
		.next = list,
		.ref_count = 1,
	};
	return w;
}

// Decrease reference count and if this is last copy then free
static void list_dealloc(struct uri_local_list *list, void (*list_free)(struct uri_local_list*)) {
	while (list) {
		list->ref_count--;
		struct uri_local_list *old = list;
		list = list->next;
		if (old->ref_count == 0) {
			list_free(old);
			free(old);
		}
	}
}


// Helper function to set default signature path if no signature set
static void ensure_default_signature(struct uri *uri) {
	if (uri->pubkey && !uri->sig_uri)
		ASSERT_MSG(uri_set_sig(uri, NULL),
			"URI creation passed so signature creation should not cause error.");
}

// This function returns URI for default parent for file scheme.
// Note that returned string is malloced and has to be freed by caller.
static char *default_file_parent() {
	char *cwd = getcwd(NULL, 0);
	ASSERT_MSG(cwd, "Unable to get current working directory");
	char *uri = malloc(8 + 3*strlen(cwd) + 1); // source: https://uriparser.github.io/doc/api/latest/index.html [+1 (/)]
	ASSERT_MSG(uriUnixFilenameToUriStringA(cwd, uri) == URI_SUCCESS,
			"CWD uri conversion failed of: %s", cwd);
	free(cwd);
	size_t len = strlen(uri);
	uri[len] = '/'; // Note: It is directory a trailing slash is required
	uri[len + 1] = '\0';
	return uri;
}

/* Note on uriparser usage here.
 * It would be definitely much more cleaner to use UriUriA during whole life of
 * uri object. Problem is that uriparser is borrowing original string. We could
 * overcame that. Bigger problem is with normalization where uriparser might
 * allocate its own string. That is not all, most notably it is also problem with
 * uriAddBaseUriA where both of those strings are reused optionally. That means
 * that we need to preserve all stings ever used to initialize any uriparser. That
 * is just stupid and to limit this behavior we instead just use canonized string
 * representation and always run parser again.
 */

// This function sets scheme and uri variable in newly created uri object
static bool canonize_uri(const char *uri_str, const struct uri *parent, struct uri *uri) {
	int urierr;
	UriUriA urip;
	// Parse uri
	// TODO we can get invalid character but how should we report it or save it?
	// (but currently ignoring it is not that big of a problem)
	if ((urierr = uriParseSingleUriA(&urip, uri_str, NULL)) != URI_SUCCESS) {
		ASSERT_MSG(urierr == URI_ERROR_SYNTAX, "Unexpected uriparser error: %d", urierr);
		uri_errno = URI_E_INVALID_URI;
		uriFreeUriMembersA(&urip);
		return false;
	}
	// Identify scheme
	uri->scheme = URI_S_UNKNOWN;
	if (urip.scheme.first != NULL) {
		size_t scheme_len = urip.scheme.afterLast - urip.scheme.first;
		for (size_t i = 0; i < URI_S_UNKNOWN; i++) {
			if (strlen(schemes_table[i]) == scheme_len &&
					! strncasecmp(schemes_table[i], urip.scheme.first, scheme_len)) {
				uri->scheme = i;
				break;
			}
		}
	} else if (parent) // No scheme means user parent
		uri->scheme = parent->scheme;
	else // No parent and no scheme we consider to be Unix path
		uri->scheme = URI_S_FILE;
	if (uri->scheme == URI_S_UNKNOWN) {
		uri_errno = URI_E_UNKNOWN_SCHEME;
		uriFreeUriMembersA(&urip);
		return false;
	}
	// TODO for data uri we should check for validity and probably should be done same for files
	// For URI it self we consider as a parent only those with same scheme
	const char *uri_parent = NULL;
	bool free_parent = false;
	if (parent && uri->scheme == parent->scheme)
		uri_parent = parent->uri;
	else if (uri->scheme == URI_S_FILE) {
		uri_parent = default_file_parent();
		free_parent = true;
	}
	if (uri_parent) {
		UriUriA parent_urip;
		// Should always be parsable because either it is cwd or was already parsed once
		ASSERT_MSG(uriParseSingleUriA(&parent_urip, uri_parent, NULL) == URI_SUCCESS,
				"Unable to parse parent URI: %s", uri_parent);
		UriUriA abs_urip;
		urierr = uriAddBaseUriA(&abs_urip, &urip, &parent_urip);
		// It should always be absolute
		ASSERT_MSG(urierr != URI_ERROR_ADDBASE_REL_BASE, "Parent URI is non-absolute: %s", uri_parent);
		ASSERT(urierr == URI_SUCCESS);
		uriFreeUriMembersA(&parent_urip);
		uriFreeUriMembersA(&urip);
		urip = abs_urip;
	}
	// Normalize URI
	ASSERT(uriNormalizeSyntaxA(&urip) == URI_SUCCESS); // No error with exception to memory ones seems to be possible here
	// Convert back to string
	int charsreq;
	ASSERT(uriToStringCharsRequiredA(&urip, &charsreq) == URI_SUCCESS);
	charsreq++;
	uri->uri = malloc(charsreq * sizeof *uri->uri);
	ASSERT(uriToStringA(uri->uri, &urip, charsreq, NULL) == URI_SUCCESS);

	// Cleanup
	uriFreeUriMembersA(&urip);
	if (uri_parent && free_parent)
		free((char*)uri_parent);
	return true;
}

static struct uri *uri_new(const char *uri_str, const struct uri *parent) {
	struct uri *ret = malloc(sizeof *ret);
	ret->finished = false;
	if (!canonize_uri(uri_str, parent, ret)) {
		free(ret);
		return NULL;
	}
	TRACE("URI new (%s) (%s): %s", uri_str, parent ? parent->uri : "none", ret->uri);
	ret->sig_uri_file = NULL;
	ret->sig_uri = NULL;
#define SET(X, DEF) do { if (parent) ret->X = parent->X; else ret->X = DEF; } while (false);
#define SET_LIST(X) do { SET(X, NULL); list_refup(ret->X); } while (false);
	SET(auto_unpack, false);
	SET(ssl_verify, true);
	SET(ocsp, true);
	SET_LIST(ca);
	SET_LIST(crl);
	SET_LIST(pubkey);
#undef SET_LIST
#undef SET
	ret->download_instance = NULL;
	return ret;
}

struct uri *uri_to_file(const char *uri, const char *output_path, const struct uri *parent) {
	struct uri *ret = uri_new(uri, parent);
	if (!ret)
		return NULL;
	ret->output_type = URI_OUT_T_FILE;
	ret->output_info.fpath = strdup(output_path);
	return ret;
}

struct uri *uri_to_temp_file(const char *uri, char *output_template, const struct uri *parent) {
	struct uri *ret = uri_new(uri, parent);
	if (!ret)
		return NULL;
	ret->output_type = URI_OUT_T_TEMP_FILE;
	ret->output_info.fpath = output_template;
	return ret;
}

struct uri *uri_to_buffer(const char *uri, const struct uri *parent) {
	struct uri *ret = uri_new(uri, parent);
	if (!ret)
		return NULL;
	ret->output_type = URI_OUT_T_BUFFER;
	ret->output_info.buf.data = NULL;
	ret->output_info.buf.size = 0;
	return ret;
}

static void list_ca_crl_free(struct uri_local_list *list);
static void list_pubkey_free(struct uri_local_list *list);

void uri_free(struct uri *uri) {
	free(uri->uri);
	if (uri->sig_uri)
		uri_free(uri->sig_uri);
	if (uri->sig_uri_file)
		free(uri->sig_uri_file);
	list_dealloc(uri->ca, list_ca_crl_free);
	list_dealloc(uri->crl, list_ca_crl_free);
	list_dealloc(uri->pubkey, list_pubkey_free);
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
			free(uri->output_info.fpath);
			break;
		case URI_OUT_T_TEMP_FILE:
			// Nothing to do
			break;
		case URI_OUT_T_BUFFER:
			if (uri->output_info.buf.data)
				free(uri->output_info.buf.data);
			break;
	}
	free(uri);
}

bool uri_is_local(const struct uri *uri) {
	switch (uri->scheme) {
		case URI_S_FILE:
		case URI_S_DATA:
			return true;
		default:
			return false;
	}
}

char *uri_path(const struct uri *uri) {
	ASSERT_MSG(uri->scheme == URI_S_FILE,
			"Called uri_path on URI of scheme: %s", uri_scheme_string(uri->scheme));
	char *path = malloc(strlen(uri->uri) - 6); // source: https://uriparser.github.io/doc/api/latest/index.html
	ASSERT_MSG(uriUriStringToUnixFilenameA(uri->uri, path) == URI_SUCCESS,
			"URI to Unix path conversion failed for: %s", uri->uri);
	return path;
}

bool uri_downloader_register(struct uri *uri, struct  downloader *downloader) {
	ASSERT_MSG(!uri->download_instance && !uri->finished,
			"uri_download_register can be called only on not yet registered uri");
	if (uri_is_local(uri))
		return true; // Just ignore if it makes no sense to call this
	ensure_default_signature(uri);
	struct download_opts opts;
	download_opts_def(&opts);
	opts.ssl_verify = uri->ssl_verify;
	opts.ocsp = uri->ocsp;
	// TODO use instead of files: https://curl.haxx.se/libcurl/c/cacertinmem.html
	if (uri->ca) {
		list_ca_crl_collect(uri->ca);
		opts.cacert_file = uri->ca->path;
		opts.capath = "/dev/null"; // disables system CAs
	}
	if (uri->crl) {
		list_ca_crl_collect(uri->crl);
		opts.crl_file = uri->crl->path;
	}
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
			uri->download_instance = download_file(downloader, uri->uri, uri->output_info.fpath, &opts);
			break;
		case URI_OUT_T_TEMP_FILE:
			uri->download_instance = download_temp_file(downloader, uri->uri, uri->output_info.fpath, &opts);
			break;
		case URI_OUT_T_BUFFER:
			uri->download_instance = download_data(downloader, uri->uri, &opts);
			break;
	}
	if (!uri->download_instance) {
		// Only reason why this would fail at the moment is if file open fails
		uri_errno = URI_E_OUTPUT_OPEN_FAIL;
		return false;
	}
	if (uri->pubkey && !uri_downloader_register(uri->sig_uri, downloader)) {
		uri_sub_errno = uri_errno;
		uri_sub_err_uri = uri->sig_uri;
		uri_errno = URI_E_SIG_FAIL;
		download_i_free(uri->download_instance);
		uri->download_instance = NULL;
		return false;
	}
	return true;
}

static FILE *uri_finish_out_f(struct uri *uri) {
	FILE *f;
	switch (uri->output_type) {
		case URI_OUT_T_TEMP_FILE:
			f = fdopen(mkstemp(uri->output_info.fpath), "w");
			break;
		case URI_OUT_T_FILE:
			f = fopen(uri->output_info.fpath, "w");
			break;
		case URI_OUT_T_BUFFER:
			f = open_memstream((char**)&uri->output_info.buf.data, &uri->output_info.buf.size);
			break;
	}
	if (f == NULL)
		uri_errno = URI_E_OUTPUT_OPEN_FAIL;
	return f;
}

static bool uri_finish_file(struct uri *uri) {
	char *srcpath = uri_path(uri);
	int fdin = open(srcpath, O_RDONLY);
	free(srcpath);
	if (fdin == -1) {
		uri_errno = URI_E_FILE_INPUT_ERROR;
		return false;
	}
	FILE *fout = uri_finish_out_f(uri);
	if (fout == NULL)
		return false;

	char buf[BUFSIZ];
	ssize_t rd;
	while ((rd = read(fdin, buf, BUFSIZ)) > 0)
		if (fwrite(buf, sizeof(char), rd, fout) != (size_t)rd) {
			close(fdin);
			fclose(fout);
			uri_errno = URI_E_OUTPUT_WRITE_FAIL;
			return false;
		}
	close(fdin);
	fclose(fout);
	return true;
}

static const char *data_param_base64 = "base64";

static bool is_archive(struct uri *uri) {
	FILE *f;
	uint16_t result;
	uint16_t magic = 0x1f8b; /* GZIP magic number */
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			f = fopen(uri->output_info.fpath, "r");
			result = (getc(f) << 8) + getc(f);
			fclose(f);
			break;
		case URI_OUT_T_BUFFER:
			result = (uri->output_info.buf.data[0] << 8) + uri->output_info.buf.data[1];
			break;
	default:
			DIE("Unsupported output type in is_archive. This should not happen.");
	}
	return magic == result;
}

static bool uri_finish_data(struct uri *uri) {
	char *start = uri->uri + 5;
	// Parameters
	bool is_base64 = false;
	char *next;
	while ((next = strchr(start, ','))) {
		if (!strncmp(data_param_base64, start, strlen(data_param_base64)))
			is_base64 = true;
		// We ignore any unsupported arguments just for compatibility
		start = next + 1;
	}

	FILE *fout = uri_finish_out_f(uri);
	if (fout == NULL)
		return false;
	if (is_base64) {
		uint8_t *buf;
		size_t buf_size;
		base64_decode(start, &buf, &buf_size);
		size_t written = fwrite(buf, 1, buf_size, fout);
		free(buf);
		if (written != buf_size) {
			uri_errno = URI_E_OUTPUT_WRITE_FAIL;
			fclose(fout);
			return false;
		}
	} else if (fputs(start, fout) <= 0) {
		fclose(fout);
		uri_errno = URI_E_OUTPUT_WRITE_FAIL;
		return false;
	}
	fclose(fout);
	return true;
}

static bool verify_signature_against(const struct uri* uri, const char *fcontent) {
	struct uri_local_list *key = uri->pubkey;
	do {
		if(!lsubprocv(LST_USIGN,
			aprintf("Verify %s (%s) against %s", uri->uri, uri->sig_uri_file, key->path),
			NULL, 30000, "usign", "-V", "-p", key->path,
			"-x", uri->sig_uri_file, "-m", fcontent, (void*)NULL))
			return true;
		key = key->next;
	} while(key);
	return false;
}

static bool verify_signature_gz(struct uri *uri) {
	char *fcontent = strdup("/tmp/updater-temp-gz-XXXXXX");
	mkstemp(fcontent);
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			upack_gz_file_to_file(uri->output_info.fpath, fcontent);
		break;
		case URI_OUT_T_BUFFER:
			upack_gz_buffer_to_file(uri->output_info.buf.data, uri->output_info.buf.size, fcontent);
			break;
		default:
			DIE("Unsupported output type in verify_signature. This should not happen.");
	}
	bool verified = verify_signature_against(uri, fcontent);
	unlink(fcontent);
	free(fcontent);
	return verified;
}

static bool verify_signature_plain(struct uri *uri) {
	char *fcontent;
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			fcontent = uri->output_info.fpath; // reuse output path
			break;
		case URI_OUT_T_BUFFER:
			fcontent = writetempfile((char*)uri->output_info.buf.data, uri->output_info.buf.size);
			break;
		default:
			DIE("Unsupported output type in verify_signature. This should not happen.");
	}
	bool verified = verify_signature_against(uri, fcontent);
	switch (uri->output_type) {
		case URI_OUT_T_FILE:
		case URI_OUT_T_TEMP_FILE:
			// Nothing to do (path reused)
			break;
		case URI_OUT_T_BUFFER:
			unlink(fcontent);
			free(fcontent);
			break;
	}
	return verified;
}

static bool verify_signature(struct uri *uri) {
	if (!uri->pubkey) // no keys means no verification
		return true;
	ASSERT_MSG(uri->sig_uri, "Signature uri should be set if public keys are provided");
	if (!uri_finish(uri->sig_uri)) {
		uri_sub_errno = uri_errno;
		uri_sub_err_uri = uri->sig_uri;
		uri_errno = URI_E_SIG_FAIL;
		return false;
	}
	uri_free(uri->sig_uri);
	uri->sig_uri = NULL;
	list_pubkey_collect(uri->pubkey);

	bool verified;
	// TODO: check also for uri->auto_unpack, but what to do then?
	if (is_archive(uri)) {
		verified = verify_signature_gz(uri);
	} else {
		verified = verify_signature_plain(uri);
	}
	free(uri->sig_uri_file);
	uri->sig_uri_file = NULL;
	if (!verified)
		uri_errno = URI_E_VERIFY_FAIL;
	return verified;
}

bool uri_finish(struct uri *uri) {
	if (uri->finished)
		return true; // Ignore if this is alredy finished
	TRACE("URI finish: %s", uri->uri);
	if (uri_is_local(uri)) {
		ensure_default_signature(uri);
		switch (uri->scheme) {
			case URI_S_FILE:
				if (!uri_finish_file(uri))
					return false;
				break;
			case URI_S_DATA:
				if (!uri_finish_data(uri))
					return false;
				break;
			default:
				DIE("Trying to finish URI that seems to be local but has unsupported scheme: %s",
						uri_scheme_string(uri->scheme));
		}
	} else {
		ASSERT_MSG(uri->download_instance, "uri_downloader_register has to be called before uri_finish");
		if (!uri->download_instance->done || !uri->download_instance->success) {
			uri_errno = uri->download_instance->done ? URI_E_DOWNLOAD_FAILED : URI_E_UNFINISHED_DOWNLOAD;
			return false;
		}
		switch (uri->output_type) {
			case URI_OUT_T_FILE:
			case URI_OUT_T_TEMP_FILE:
				// Nothing to do (data are already in file)
				download_i_free(uri->download_instance);
				break;
			case URI_OUT_T_BUFFER:
				download_i_collect_data(uri->download_instance, &uri->output_info.buf.data, &uri->output_info.buf.size);
				break;
		}
		uri->download_instance = NULL;
	}
	uri->finished = true;
	return verify_signature(uri);
}

void uri_take_buffer(struct uri *uri, uint8_t **buffer, size_t *len) {
	ASSERT_MSG(uri->output_type == URI_OUT_T_BUFFER, "URI is not of buffer output type: %s", uri->uri);
	ASSERT_MSG(uri->finished, "URI has to be finished before requesting buffers: %s", uri->uri);
	*buffer = uri->output_info.buf.data;
	*len = uri->output_info.buf.size;
	uri->output_info.buf.data = NULL;
	uri->output_info.buf.size = 0;
}

const char *uri_error_msg(enum uri_error err) {
	return error_messages[err];
}

const char *uri_download_error(struct uri *uri) {
	ASSERT_MSG(uri->download_instance, "uri_download_error can be called only on URIs with registered downloader.");
	ASSERT_MSG(uri->download_instance->done, "uri_download_error can be called only after downloader_run.");
	ASSERT_MSG(!uri->download_instance->success, "uri_download_error can be called only on failed URIs.");
	return uri->download_instance->error;
}

const char *uri_scheme_string(enum uri_scheme scheme) {
	return schemes_table[scheme];
}

#define CONFIG_GUARD ASSERT_MSG(!uri->download_instance && !uri->finished, \
		"(%s) URI configuration can't be changed after uri_register_downloader and uri_finish", uri->uri)

void uri_set_auto_unpack(struct uri *uri, bool unpack) {
	CONFIG_GUARD;
	TRACE("URI auto unpack (%s): $%s", uri->uri, STRBOOL(unpack));
	uri->auto_unpack = unpack;
}

void uri_set_ssl_verify(struct uri *uri, bool verify) {
	CONFIG_GUARD;
	TRACE("URI ssl verify (%s): $%s", uri->uri, STRBOOL(verify));
	uri->ssl_verify = verify;
}

// Generate temporally file from all subsequent certificates (and CRLs)
static void list_ca_crl_collect(struct uri_local_list *list) {
	if (!list || list->path)
		return; // not set or already collected to file so all is done

	unsigned refs = 0;
	struct mwrite mw;
	mwrite_init(&mw);
	do {
		if (list->uri) {
			if (list->ref_count > refs) {
				list->path = strdup(TMP_TEMPLATE_CA_CRL_FILE);
				ASSERT(mwrite_mkstemp(&mw, list->path, 0));
				refs = list->ref_count;
			}
			if (uri_finish(list->uri)) {
				uint8_t *buf;
				size_t buf_size;
				uri_take_buffer(list->uri, &buf, &buf_size);
				ASSERT(mwrite_write(&mw, buf, buf_size) == MWRITE_R_OK);
				free(buf);
			} else
				DBG("Unable to get CA/CRL %s: %s", list->uri->uri, uri_error_msg(uri_errno));
			uri_free(list->uri);
			list->uri = NULL;
		} else {
			// This is already collected to file so we read file it self.
			int fd = open(list->path, 0, O_RDONLY);
			char *buf[BUFSIZ];
			ssize_t cnt;
			while((cnt = read(fd, buf, BUFSIZ)) > 0)
				ASSERT(mwrite_write(&mw, buf, cnt) == MWRITE_R_OK);
			close(fd);
			break;
		}
		list = list->next;
	} while (list);

	if (!mwrite_close(&mw)) {
		uri_sub_errno = URI_E_OUTPUT_WRITE_FAIL;
		uri_sub_err_uri = NULL;
	}
}

// deallocation handler for CA and CRL list
static void list_ca_crl_free(struct uri_local_list *list) {
	if (list->uri)
		uri_free(list->uri);
	if (list->path) {
		unlink(list->path);
		free(list->path);
	}
}

// Common add function for both CA and CRL
static bool list_ca_crl_add(const char *str_uri, struct uri_local_list **list, const char *what, const char *for_uri) {
	if (!str_uri){
		TRACE("URI all %ss dropped (%s)", what, for_uri);
		list_dealloc(*list, list_ca_crl_free);
		*list = NULL;
		return true;
	}
	struct uri *nuri = uri_to_buffer(str_uri, NULL);
	if (!nuri)
		return false;
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		uri_free(nuri);
		return false;
	}
	*list = list_add(*list);
	(*list)->uri = nuri;
	(*list)->path = NULL;
	TRACE("URI added %s (%s): %s", what, for_uri, nuri->uri);
	return true;
}

bool uri_add_ca(struct uri *uri, const char *ca_uri) {
	CONFIG_GUARD;
	return list_ca_crl_add(ca_uri, &uri->ca, "CA", uri->uri);
}

bool uri_add_crl(struct uri *uri, const char *crl_uri) {
	CONFIG_GUARD;
	return list_ca_crl_add(crl_uri, &uri->crl, "CRL", uri->uri);
}

void uri_set_ocsp(struct uri *uri, bool enabled) {
	CONFIG_GUARD;
	uri->ocsp = enabled;
	TRACE("URI OCSP (%s): $%s", uri->uri, STRBOOL(enabled));
}

// Generate temporally file from all subsequent public keys
static void list_pubkey_collect(struct uri_local_list *list) {
	while (list && list->uri) {
		if (!uri_finish(list->uri))
			DBG("Unable to get pubkey %s: %s", list->uri->uri, uri_error_msg(uri_errno));
		uri_free(list->uri);
		list->uri = NULL;
		list = list->next;
	}
}

// deallocation handler for pubkey list
static void list_pubkey_free(struct uri_local_list *list) {
	if (list->uri)
		uri_free(list->uri);
	if (list->path) {
		unlink(list->path); // Intentionally ignoring error (if no file was created)
		free(list->path);
	}
}

bool uri_add_pubkey(struct uri *uri, const char *pubkey_uri) {
	CONFIG_GUARD;
	if (!pubkey_uri) {
		list_dealloc(uri->pubkey, list_pubkey_free);
		uri->pubkey = NULL;
		return true;
	}
	// TODO we can reuse file path but can't automatically remove such file on cleanup
	char *file_path = strdup(TMP_TEMPLATE_PUBKEY_FILE);
	struct uri *nuri = uri_to_temp_file(pubkey_uri, file_path, NULL);
	if (!nuri)
		goto error;
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		goto error;
	}
	uri->pubkey = list_add(uri->pubkey);
	uri->pubkey->uri = nuri;
	uri->pubkey->path = file_path;
	TRACE("URI added pubkey (%s): %s", uri->uri, nuri->uri);
	return true;

error:
	if (nuri)
		uri_free(nuri);
	free(file_path);
	return false;
}

bool uri_set_sig(struct uri *uri, const char *sig_uri) {
	CONFIG_GUARD;
	if (uri->sig_uri) // Free any previous uri
		uri_free(uri->sig_uri);

	if (!sig_uri) {
		int pos = strlen(uri->uri) - 3;
		char changed = '\0';
		// Remove .gz if present, signature matches unpacked file
		if (!strcmp(uri->uri + pos, ".gz")) {
			changed = uri->uri[pos];
			uri->uri[pos] = '\0';
		}
		sig_uri = aprintf("%s.sig", uri->uri);
		if (changed)
			uri->uri[pos] = changed;
	}
	uri->sig_uri_file = strdup(TMP_TEMPLATE_SIGNATURE_FILE);
	uri->sig_uri = uri_to_temp_file(sig_uri, uri->sig_uri_file, uri);
	if (!uri->sig_uri)
		return false;
	uri_add_pubkey(uri->sig_uri, NULL); // Reset public keys (verification is not possible)
	TRACE("URI signature set (%s): %s", uri->uri, uri->sig_uri->uri);
	return true;
}
