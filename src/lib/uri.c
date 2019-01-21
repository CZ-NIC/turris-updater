/*
 * Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <uriparser/Uri.h>

#define TMP_TEMPLATE_CA_CRL_FILE "/tmp/updater-ca-XXXXXX"
#define TMP_TEMPLATE_PUBKEY_FILE "/tmp/updater-pubkey-XXXXXX"
#define TMP_TEMPLATE_SIGNATURE_FILE "/tmp/updater-sig-XXXXXX"


thread_local enum uri_error uri_errno = 0;
thread_local static int uriparser_errno;


static const char *schemes_table[] = {
	[URI_S_HTTP] = "http",
	[URI_S_HTTPS] = "https",
	[URI_S_FILE] = "file",
	[URI_S_DATA] = "data",
};


static bool list_ca_crl_collect(struct uri_local_list*);
static bool list_pubkey_collect(struct uri_local_list*);

// Bup reference count
static void list_refup(struct uri_local_list *list) {
	while(list) {
		list->ref_count++;
		list = list->next;
	}
}

// Generic function to add item to list used by all following add functions
static struct uri_local_list *list_add(struct uri_local_list *list, void (*free_func)(struct uri_local_list*)) {
	list_refup(list);
	struct uri_local_list *w = malloc(sizeof *w);
	*w = (struct uri_local_list) {
		.next = list,
		.ref_count = 1,
		.free = free_func,
	};
	return w;
}

// Decrease reference count and if this is last copy then free
static void list_dealloc(struct uri_local_list *list) {
	while (list) {
		list->ref_count--;
		struct uri_local_list *old = list;
		list = list->next;
		if (old->ref_count == 0) {
			old->free(list);
			free(old->uri);
			free(old);
		}
	}
}


// This function returns URI for default parent for file scheme.
// Note that returned string is malloced and has to be freed by caller.
static char *default_file_parent() {
	char *cwd = getcwd(NULL, 0);
	if (!cwd) {
		uri_errno = URI_E_GETCWD;
		return NULL;
	}
	char *uri = malloc(8 + 3*strlen(cwd) + 1); // source: https://uriparser.github.io/doc/api/latest/index.html
	uriparser_errno = uriUnixFilenameToUriStringA(cwd, uri);
	free(cwd);
	if (uriparser_errno != URI_SUCCESS) {
		uri_errno = URI_E_CWD2URI;
		free(uri);
		return NULL;
	}
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

static bool uri_apply_parent(UriUriA *uri, const char *parent) {
	UriUriA parent_urip;
	if (uriParseSingleUriA(&parent_urip, parent, NULL) != URI_SUCCESS) {
		uriFreeUriMembersA(&parent_urip);
		// TODO report error
		return false;
	}
	UriUriA abs_urip;
	if (uriAddBaseUriA(&abs_urip, uri, &parent_urip) != URI_SUCCESS) {
		uriFreeUriMembersA(&parent_urip);
		uriFreeUriMembersA(&abs_urip);
		// TODO report error
		return false;
	}
	uriFreeUriMembersA(&parent_urip);
	uriFreeUriMembersA(uri);
	*uri = abs_urip;
	return true;
}

// This function sets scheme and uri variable in newly created uri object
static bool canonize_uri(const char *uri_str, const struct uri *parent, struct uri *uri) {
	bool success = true;
	UriUriA urip;
	// Parse passed uri
	const char *error_char; // TODO
	if (uriParseSingleUriA(&urip, uri_str, &error_char) != URI_SUCCESS) {
		// TODO report error
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
		// TODO error that this scheme is unknown
		uriFreeUriMembersA(&urip);
		return NULL;
	}
	// For URI it self we consider as a parent only those with same scheme
	const char *uri_parent = NULL;
	bool free_parent = false;
	if (parent && uri->scheme == parent->scheme)
		uri_parent = parent->uri;
	else if (uri->scheme == URI_S_FILE) {
		uri_parent = default_file_parent();
		free_parent = true;
	}
	if (uri_parent)
		if (!uri_apply_parent(&urip, uri_parent))
			goto handle_error;
	// Normalize URI
	if (uriNormalizeSyntaxA(&urip) != URI_SUCCESS) {
		// TODO report error
		goto handle_error;
	}
	// Convert back to string
	int charsreq;
	if (uriToStringCharsRequiredA(&urip, &charsreq) != URI_SUCCESS) {
		// TODO report error
		goto handle_error;
	}
	charsreq++;
	uri->uri = malloc(charsreq * sizeof *uri->uri);
	if (uriToStringA(uri->uri, &urip, charsreq, NULL) != URI_SUCCESS) {
		// TODO report error
		free(uri->uri);
		uri->uri = NULL;
		goto handle_error;
	}

	goto cleanup;
handle_error:
	success = false;

cleanup:
	uriFreeUriMembersA(&urip);
	if (uri_parent && free_parent)
		free((char*)uri_parent);

	return success;
}

static struct uri *uri_new(const char *uri_str, const struct uri *parent) {
	struct uri *ret = malloc(sizeof *ret);
	ret->finished = false;
	if (!canonize_uri(uri_str, parent, ret)) {
		// TODO error
		free(ret);
		return NULL;
	}
	ret->sig_uri_file = NULL;
	ret->sig_uri = NULL;
#define SET(X, DEF) do { if (parent) ret->X = parent->X; else ret->X = DEF; } while (false);
#define SET_LIST(X) do { SET(X, NULL); list_refup(ret->X); } while (false);
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

void uri_free(struct uri *uri) {
	free(uri->uri);
	if (uri->sig_uri)
		uri_free(uri->sig_uri);
	if (uri->sig_uri_file)
		free(uri->sig_uri_file);
	list_dealloc(uri->ca);
	list_dealloc(uri->crl);
	list_dealloc(uri->pubkey);
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
	char *path = malloc(strlen(uri->uri) + 1); // source: https://uriparser.github.io/doc/api/latest/index.html
	if (uriUriStringToUnixFilenameA(uri->uri, path) != URI_SUCCESS) {
		// TODO report error
		free(path);
		return NULL;
	}
	return path;
}

bool downloader_register_signature(struct uri *uri, struct downloader *downloader) {
	if (!uri->pubkey)
		return true;
	if (!uri->sig_uri)
		uri_set_sig(uri, NULL);
	if (!list_pubkey_collect(uri->pubkey))
		return false;
	return uri_downloader_register(uri->sig_uri, downloader);
}

bool uri_downloader_register(struct uri *uri, struct  downloader *downloader) {
	if (uri_is_local(uri)) {
		// TODO error
		return false;
	}
	struct download_opts opts;
	download_opts_def(&opts);
	opts.ssl_verify = uri->ssl_verify;
	opts.ocsp = uri->ocsp;
	if (uri->ca) {
		if (!list_ca_crl_collect(uri->ca)) {
			// TODO error
			return false;
		}
		opts.cacert_file = uri->ca->path;
		opts.capath = "/dev/null"; // disables system CAs
	}
	if (uri->crl) {
		if (!list_ca_crl_collect(uri->crl)) {
			// TODO error
			return false;
		}
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
	if (!downloader_register_signature(uri, downloader)) {
		download_i_free(uri->download_instance);
		uri->download_instance = NULL;
		// TODO error
		return false;
	}
	return (bool)uri->download_instance;
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
	// TODO check for error
	return f;
}

static bool uri_finish_file(struct uri *uri) {
	char *srcpath = uri_path(uri);
	int fdin = open(srcpath, O_RDONLY);
	if (fdin == -1) {
		// TODO error
		return false;
	}
	free(srcpath);
	FILE *fout = uri_finish_out_f(uri);

	char buf[BUFSIZ];
	ssize_t rd;
	while ((rd = read(fdin, buf, BUFSIZ)) > 0) {
		if (fwrite(buf, sizeof(char), rd, fout) != (size_t)rd) {
			close(fdin);
			fclose(fout);
			// TODO set error
			return false;
		}
	}
	close(fdin);
	fclose(fout);
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

	FILE *fout = uri_finish_out_f(uri);
	if (is_base64) {
		// TODO
		fclose(fout);
		return false;
	} else if (fputs(start, fout) <= 0) {
		fclose(fout);
		// TODO error
		return false;
	}
	fclose(fout);
	return true;
}

bool uri_finish(struct uri *uri) {
	if (uri->finished) {
		// TODO error
		return false;
	}
	if (uri_is_local(uri)) {
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
				// TODO error
				return false;
		}
	} else {
		if (uri->download_instance) {
			if (!uri->download_instance->done || !uri->download_instance->success)
				// TODO error (either not completed or failed download)
				return false;
		} else {
			// TODO error you have to register downloader first
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
	if (uri->pubkey) {
		if (!uri_finish(uri->sig_uri)) {
			// TODO error
			return false;
		}
		uri_free(uri->sig_uri);
		uri->sig_uri = NULL;
		if (!list_pubkey_collect(uri->pubkey)) {
			// TODO error
			return false;
		}
		struct uri_local_list *key = uri->pubkey;
		do {
			// TODO call usign to verify
			key = key->next;
		} while(key);
		return false;
	}
	return true;
}

bool uri_take_buffer(struct uri *uri, uint8_t **buffer, size_t *len) {
	if (uri->output_type != URI_OUT_T_BUFFER)
		// TODO error
		return false;
	if (!uri->finished)
		// TODO error
		return false;
	*buffer = uri->output_info.buf.data;
	*len = uri->output_info.buf.size;
	uri->output_info.buf.data = NULL;
	uri->output_info.buf.size = 0;
	return true;
}

char *uri_error_msg(struct uri *uri) {
	// TODO
	return NULL;
}

// TODO we should not allow configuration change after downloader is registered
// and uri is finished

bool uri_set_ssl_verify(struct uri *uri, bool verify) {
	uri->ssl_verify = verify;
	return true;
}

// Generate temporally file from all subsequent certificates (and CRLs)
static bool list_ca_crl_collect(struct uri_local_list *list) {
	if (!list || list->path)
		return true; // not set or already collected to file so all is done

	unsigned refs = 0;
	struct mwrite mw;
	mwrite_init(&mw);
	do {
		if (list->uri) {
			if (list->ref_count > refs) {
				list->path = strdup(TMP_TEMPLATE_CA_CRL_FILE);
				mwrite_mkstemp(&mw, list->path, 0); // TODO handle error
				refs = list->ref_count;
			}
			if (!uri_finish(list->uri)) {
				// TODO error
				return false;
			}
			uint8_t *buf;
			size_t buf_size;
			uri_take_buffer(list->uri, &buf, &buf_size);
			mwrite_write(&mw, buf, buf_size); // TODO handler error
			uri_free(list->uri);
			list->uri = NULL;
		} else {
			// This is already collected to file so we read file it self.
			int fd = open(list->path, 0, O_RDONLY);
			char *buf[BUFSIZ];
			ssize_t cnt;
			while((cnt = read(fd, buf, BUFSIZ)) > 0)
				mwrite_write(&mw, buf, cnt); // TODO handle error
			close(fd);
			break;
		}
		list = list->next;
	} while (list);
	mwrite_close(&mw); // TODO handle error
	return true;
}

// deallocation handler for CA and CRL list
static void list_ca_crl_free(struct uri_local_list *list) {
	if (!list)
		return;
	if (list->uri)
		uri_free(list->uri);
	if (list->path) {
		unlink(list->path);
		free(list->path);
	}
}

// Common add function for both CA and CRL
static bool list_ca_crl_add(struct uri *uri, const char *str_uri, struct uri_local_list **list) {
	if (!str_uri){
		list_dealloc(*list);
		*list = NULL;
		return true;
	}
	struct uri *nuri = uri_to_buffer(str_uri, uri);
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		uri_free(nuri);
		return false;
	}
	*list = list_add(*list, list_ca_crl_free);
	(*list)->uri = nuri;
	(*list)->path = NULL;
	return true;
}

bool uri_add_ca(struct uri *uri, const char *ca_uri) {
	return list_ca_crl_add(uri, ca_uri, &uri->ca);
}

bool uri_add_crl(struct uri *uri, const char *crl_uri) {
	return list_ca_crl_add(uri, crl_uri, &uri->crl);
}

bool uri_set_ocsp(struct uri *uri, bool enabled) {
	uri->ocsp = enabled;
	return true;
}

// Generate temporally file from all subsequent public keys
static bool list_pubkey_collect(struct uri_local_list *list) {
	while (list && list->uri) {
		if (uri_finish(list->uri)) {
			// TODO error
			return false;
		}
		list->uri = NULL;
		list = list->next;
	}
	return true;
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
	if (!pubkey_uri){
		list_dealloc(uri->pubkey);
		uri->pubkey = NULL;
		return true;
	}
	// TODO if uri is already of type FILE then reuse posix path and don't download
	char *file_path = strdup(TMP_TEMPLATE_PUBKEY_FILE);
	struct uri *nuri = uri_to_temp_file(pubkey_uri, file_path, uri);
	if (!uri_is_local(nuri)) {
		uri_errno = URI_E_NONLOCAL;
		uri_free(nuri);
		free(file_path);
		return false;
	}
	uri->pubkey = list_add(uri->pubkey, list_pubkey_free);
	uri->pubkey->uri = nuri;
	uri->pubkey->path = file_path;
	return true;
}

bool uri_set_sig(struct uri *uri, const char *sig_uri) {
	if (uri->sig_uri) // Free any previous uri
		uri_free(uri->sig_uri);

	if (!sig_uri)
		sig_uri = aprintf("%s.sig", uri->uri);
	uri->sig_uri_file = strdup(TMP_TEMPLATE_SIGNATURE_FILE);
	uri->sig_uri = uri_to_temp_file(sig_uri, uri->sig_uri_file, uri);
	return (bool)uri->sig_uri;
}
