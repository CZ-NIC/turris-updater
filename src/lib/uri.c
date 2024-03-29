/*
 * Copyright 2018-2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "signature.h"
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <uriparser/Uri.h>
#include <base64c.h>


THREAD_LOCAL enum uri_error uri_errno = 0;
THREAD_LOCAL enum uri_error uri_sub_errno = 0;
THREAD_LOCAL struct uri *uri_sub_err_uri = NULL;

static const char *error_messages[] = {
	[URI_E_INVALID_URI] = "URI has invalid format",
	[URI_E_UNKNOWN_SCHEME] = "URI contains invalid or unsupported scheme",
	[URI_E_UNFINISHED_DOWNLOAD] = "Download wasn't finished or even started",
	[URI_E_DOWNLOAD_FAIL] = "Download failed",
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

// This implements list of local URI handlers
struct uri_local_list {
	struct uri_local_list *next; // Link to (next) previous provided uri
	unsigned ref_count; // Reference counter (counts number of usages in uri object)

	struct uri *uri; // Uri object initialized by URI provided by user
	union {
		struct sign_pubkey *pubkey;
		struct download_pem *pem;
	} dt;
};

// URI representation
struct uri {
	enum uri_scheme scheme;
	bool finished;
	char *uri; // Uri string in canonical format

	FILE *output;
	uint8_t *data;
	size_t data_len;

	struct download_i *download_instance;

	// HTTPS options
	bool ssl_verify; // If SSL should be verified
	bool ocsp; // If OCSP should be used for ceritification validity check
	bool ca_pin; // If system CAs should not be used (certification pinning)
	struct uri_local_list *pem; // List of all configured CAs and CRLs (PEM)
	// Signature verification
	struct uri_local_list *pubkey; // URIs to public keys used for verification
	struct uri *sig_uri; // signature URI
};

static struct download_pem **list_pem_collect(struct uri_local_list*, size_t level);
static struct sign_pubkey **list_pubkey_collect(struct uri_local_list*, size_t level);

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

uri_t uri(const char *uri_str, const uri_t parent) {
	struct uri *ret = malloc(sizeof *ret);
	ret->finished = false;
	if (!canonize_uri(uri_str, parent, ret)) {
		free(ret);
		return NULL;
	}
	TRACE("URI new (%s) (%s): %s", uri_str, parent ? parent->uri : "none", ret->uri);
	ret->sig_uri = NULL;
#define SET(X, DEF) do { if (parent) ret->X = parent->X; else ret->X = DEF; } while (false);
#define SET_LIST(X) do { SET(X, NULL); list_refup(ret->X); } while (false);
	SET(ssl_verify, true);
	SET(ocsp, true);
	SET(ca_pin, false);
	SET_LIST(pem);
	SET_LIST(pubkey);
#undef SET_LIST
#undef SET
	ret->output = NULL;
	ret->data = NULL;
	ret->data_len = 0;
	ret->download_instance = NULL;
	return ret;
}

static void list_pem_free(struct uri_local_list *list);
static void list_pubkey_free(struct uri_local_list *list);

void uri_free(struct uri *uri) {
	free(uri->uri);
	if (uri->sig_uri)
		uri_free(uri->sig_uri);
	list_dealloc(uri->pem, list_pem_free);
	list_dealloc(uri->pubkey, list_pubkey_free);
	if (uri->output)
		fclose(uri->output);
	if (uri->data)
		free(uri->data);
	free(uri);
}

const char *uri_uri(const uri_t u) {
	return u->uri;
}

enum uri_scheme uri_scheme(const uri_t u) {
	return u->scheme;
}

bool uri_is_local(const uri_t uri) {
	switch (uri->scheme) {
		case URI_S_FILE:
		case URI_S_DATA:
			return true;
		default:
			return false;
	}
}

char *uri_path(const uri_t uri) {
	ASSERT_MSG(uri->scheme == URI_S_FILE,
			"Called uri_path on URI of scheme: %s", uri_scheme_string(uri->scheme));
	char *path = malloc(strlen(uri->uri) - 6); // source: https://uriparser.github.io/doc/api/latest/index.html
	ASSERT_MSG(uriUriStringToUnixFilenameA(uri->uri, path) == URI_SUCCESS,
			"URI to Unix path conversion failed for: %s", uri->uri);
	return path;
}

#define OUTPUT_GUARD ASSERT_MSG(!u->output && !u->finished, "(%s) URI output can't be changed", u->uri)

bool uri_output_file(uri_t u, const char *path) {
	OUTPUT_GUARD;
	u->output = fopen(path, "w+");
	if (u->output)
		return true;
	uri_errno = URI_E_OUTPUT_OPEN_FAIL;
	return false;
}

bool uri_output_tmpfile(uri_t u, char *path_template) {
	OUTPUT_GUARD;
	int fd = mkstemp(path_template);
	if (fd == -1) {
		uri_errno = URI_E_OUTPUT_OPEN_FAIL;
		return false;
	}
	u->output = fdopen(fd, "w+");
	return true;
}

#undef OUTPUT_GUARD

static void ensure_output(uri_t uri) {
	if (!uri->output)
		uri->output = open_memstream((char**)&uri->data, &uri->data_len);
}

bool uri_downloader_register(uri_t uri, downloader_t downloader) {
	ASSERT_MSG(!uri->download_instance && !uri->finished,
		"uri_download_register can be called only on not yet registered uri");
	if (uri_is_local(uri))
		return true; // Just ignore local URIs as we have nothing to download
	ensure_output(uri);
	ensure_default_signature(uri);

	struct download_pem **pems = list_pem_collect(uri->pem, 0);

	struct download_opts opts;
	download_opts_def(&opts);
	opts.ssl_verify = uri->ssl_verify;
	opts.ocsp = uri->ocsp;
	opts.pems = pems;
	if (uri->ca_pin) {
		opts.cacert_file = NULL;
		opts.capath = NULL;
	}
	uri->download_instance = download(downloader, uri->uri, uri->output, &opts);
	free(pems);

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

download_i_t uri_download_instance(uri_t u) {
	return u->download_instance;
}

static bool uri_finish_file(struct uri *uri) {
	char *srcpath = uri_path(uri);
	int fdin = open(srcpath, O_RDONLY);
	free(srcpath);
	if (fdin == -1) {
		uri_errno = URI_E_FILE_INPUT_ERROR;
		return false;
	}

	char buf[BUFSIZ];
	ssize_t rd;
	while ((rd = read(fdin, buf, BUFSIZ)) > 0)
		if (fwrite(buf, sizeof(char), rd, uri->output) != (size_t)rd) {
			close(fdin);
			uri_errno = URI_E_OUTPUT_WRITE_FAIL;
			return false;
		}
	close(fdin);
	return true;
}

static const char *data_param_base64 = "base64";

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
	size_t len = strlen(start);

	if (is_base64) {
		uint8_t *buf;
		size_t bufsiz = base64_mdecode(start, len, &buf);
		size_t written = fwrite(buf, 1, bufsiz, uri->output);
		free(buf);
		if (written != bufsiz) {
			uri_errno = URI_E_OUTPUT_WRITE_FAIL;
			return false;
		}
	} else if (fputs(start, uri->output) <= 0) {
		uri_errno = URI_E_OUTPUT_WRITE_FAIL;
		return false;
	}
	return true;
}

static bool verify_signature(struct uri *uri) {
	if (!uri->pubkey) // no keys means no verification
		return true;
	ASSERT_MSG(uri->sig_uri, "Signature uri should be set if public keys are provided (URI: %s)", uri->uri);
	const uint8_t *sign;
	size_t sign_len;
	if (!uri_finish(uri->sig_uri, &sign, &sign_len)) {
		uri_sub_errno = uri_errno;
		uri_sub_err_uri = uri->sig_uri;
		uri_errno = URI_E_SIG_FAIL;
		return false;
	}

	struct sign_pubkey **pubkeys = list_pubkey_collect(uri->pubkey, 0);

	uint8_t *data;
	size_t data_len;
	if (uri->data) {
		data = uri->data;
		data_len = uri->data_len;
	} else {
		data_len = ftell(uri->output);
		ASSERT((data = mmap(NULL, data_len, PROT_READ, MAP_PRIVATE, fileno(uri->output), 0)) != MAP_FAILED);
	}

	bool verified = sign_verify(data, data_len, sign, sign_len,
			(const struct sign_pubkey* const*)pubkeys);
	if (!verified) {
		DBG("URI (%s) verify failed; %s", uri->uri, sign_strerror(sign_errno));
		uri_errno = URI_E_VERIFY_FAIL;
	}

	if (!uri->data)
		munmap(data, data_len);
	free(pubkeys);
	uri_free(uri->sig_uri);
	uri->sig_uri = NULL;

	return verified;
}

bool uri_finish(uri_t uri, const uint8_t **data, size_t *len) {
	if (uri->finished)
		goto tail;
	TRACE("URI finish: %s", uri->uri);
	if (uri_is_local(uri)) {
		ensure_output(uri);
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
		if (!download_is_done(uri->download_instance) || !download_is_success(uri->download_instance)) {
			uri_errno = download_is_done(uri->download_instance) ? URI_E_DOWNLOAD_FAIL : URI_E_UNFINISHED_DOWNLOAD;
			return false;
		}
		download_i_free(uri->download_instance);
		uri->download_instance = NULL;
	}
	fflush(uri->output);
	uri->finished = true;
	if (!verify_signature(uri))
		return false;
	fclose(uri->output);
	uri->output = NULL;
tail:
	if (data)
		*data = uri->data;
	if (len)
		*len = uri->data_len;
	return true;
}

const char *uri_error_msg(enum uri_error err) {
	return error_messages[err];
}

const char *uri_download_error(struct uri *uri) {
	ASSERT_MSG(uri->download_instance, "uri_download_error can be called only on URIs with registered downloader.");
	ASSERT_MSG(download_is_done(uri->download_instance), "uri_download_error can be called only after downloader_run.");
	ASSERT_MSG(!download_is_success(uri->download_instance), "uri_download_error can be called only on failed URIs.");
	return download_error(uri->download_instance);
}

const char *uri_scheme_string(enum uri_scheme scheme) {
	return schemes_table[scheme];
}

#define CONFIG_GUARD ASSERT_MSG(!u->download_instance && !u->finished, \
		"(%s) URI configuration can't be changed after uri_register_downloader and uri_finish", u->uri)

void uri_set_ssl_verify(uri_t u, bool verify) {
	CONFIG_GUARD;
	TRACE("URI ssl verify (%s): $%s", u->uri, STRBOOL(verify));
	u->ssl_verify = verify;
}

static struct download_pem **list_pem_collect(struct uri_local_list *list, size_t level) {
	if (!list) { // lowest level so allocate appropriate size of array
		struct download_pem **pems = malloc((level + 1) * sizeof *pems);
		pems[level] = NULL;
		return pems;
	}

	if (list->uri) {
		const uint8_t *data;
		size_t len;
		if (uri_finish(list->uri, &data, &len)) {
			list->dt.pem = download_pem(data, len); // TODO error?
		} else
			DBG("Unable to get CA/CRL %s: %s", list->uri->uri, uri_error_msg(uri_errno));
		uri_free(list->uri);
		list->uri = NULL;
	}

	struct download_pem **pems = list_pem_collect(list->next,
			list->dt.pem ? level + 1 : level);
	if (list->dt.pem)
		pems[level] = list->dt.pem;
	return pems;
}

// deallocation handler for CA and CRL list
static void list_pem_free(struct uri_local_list *list) {
	if (list->uri)
		uri_free(list->uri);
	if (list->dt.pem)
		download_pem_free(list->dt.pem);
}

bool uri_add_pem(uri_t u, const char *pem_uri) {
	CONFIG_GUARD;
	if (!pem_uri){
		TRACE("URI all PEMs (CAs and CRLs) dropped (%s)", u->uri);
		list_dealloc(u->pem, list_pem_free);
		u->pem = NULL;
		return true;
	}
	struct uri *nuri = uri(pem_uri, NULL);
	if (!nuri)
		return false;
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		uri_free(nuri);
		return false;
	}
	u->pem = list_add(u->pem);
	u->pem->uri = nuri;
	u->pem->dt.pem = NULL;
	TRACE("URI added PEM (%s): %s", u->uri, nuri->uri);
	return true;
}

void uri_set_ca_pin(uri_t u, bool enabled) {
	CONFIG_GUARD;
	u->ca_pin = enabled;
	TRACE("URI CA pin (%s): $%s", u->uri, STRBOOL(enabled));
}

void uri_set_ocsp(uri_t u, bool enabled) {
	CONFIG_GUARD;
	u->ocsp = enabled;
	TRACE("URI OCSP (%s): $%s", u->uri, STRBOOL(enabled));
}

static struct sign_pubkey **list_pubkey_collect(struct uri_local_list *list, size_t level) {
	if (!list) {
		struct sign_pubkey **pubkeys = malloc((level + 1) * sizeof *pubkeys);
		pubkeys[level] = NULL;
		return pubkeys;
	}

	if (list->uri) {
		const uint8_t *data;
		size_t len;
		if (uri_finish(list->uri, &data, &len))
			list->dt.pubkey = sign_pubkey(data, len); // TODO error?
		else
			DBG("Unable to get pubkey %s: %s", list->uri->uri, uri_error_msg(uri_errno));
		uri_free(list->uri);
		list->uri = NULL;
	}

	struct sign_pubkey **pubkeys = list_pubkey_collect(list->next,
			list->dt.pubkey ? level + 1 : level);
	if (list->dt.pubkey)
		pubkeys[level] = list->dt.pubkey;
	return pubkeys;
}

// deallocation handler for pubkey list
static void list_pubkey_free(struct uri_local_list *list) {
	if (list->uri)
		uri_free(list->uri);
	if (list->dt.pubkey)
		sign_pubkey_free(list->dt.pubkey);
}

bool uri_add_pubkey(uri_t u, const char *pubkey_uri) {
	CONFIG_GUARD;
	if (!pubkey_uri) {
		list_dealloc(u->pubkey, list_pubkey_free);
		u->pubkey = NULL;
		return true;
	}
	struct uri *nuri = uri(pubkey_uri, NULL);
	if (!nuri)
		return false;
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		uri_free(nuri);
		return false;
	}

	u->pubkey = list_add(u->pubkey);
	u->pubkey->uri = nuri;
	u->pubkey->dt.pubkey = NULL;
	TRACE("URI added pubkey (%s): %s", u->uri, nuri->uri);
	return true;
}

bool uri_set_sig(uri_t u, const char *sig_uri) {
	CONFIG_GUARD;
	if (u->sig_uri) // Free any previous uri
		uri_free(u->sig_uri);

	if (!sig_uri)
		sig_uri = aprintf("%s.sig", u->uri);
	u->sig_uri = uri(sig_uri, u);
	if (!u->sig_uri)
		return false;
	uri_add_pubkey(u->sig_uri, NULL); // Reset public keys (verification is not possible)
	TRACE("URI signature set (%s): %s", u->uri, u->sig_uri->uri);
	return true;
}
