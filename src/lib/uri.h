/*
 * Copyright 2019-2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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
#ifndef UPDATER_URI_H
#define UPDATER_URI_H
#include <stdint.h>
#include <stdbool.h>
#include "download.h"
#include "util.h"

struct uri;
typedef struct uri* uri_t;

enum uri_error {
	URI_E_INVALID_URI, // Parsed URI was deemed as invalid
	URI_E_UNKNOWN_SCHEME, // URI scheme is not supported
	URI_E_UNFINISHED_DOWNLOAD, // URI was not downloaded yet
	URI_E_DOWNLOAD_FAIL, // URI download failed
	URI_E_FILE_INPUT_ERROR, // Failure of opening file as an input (file://) (use errno for error)
	URI_E_OUTPUT_OPEN_FAIL, // Unable to open output (file) for writing (use errno for error)
	URI_E_OUTPUT_WRITE_FAIL, // Writing to output resulted to failure (use errno for error)
	URI_E_SIG_FAIL, // Getting configured signature failed (see uri_sub_errno)
	URI_E_VERIFY_FAIL, // Signature does not match any public key or content does not hold integrity
	URI_E_NONLOCAL, // Configuration URI is not of local type
};

// URI error number
extern THREAD_LOCAL enum uri_error uri_errno;
// Error that is set when uri_errno is set to URI_E_SIG_FAIL
extern THREAD_LOCAL enum uri_error uri_sub_errno;
// URI object that caused uri_sub_errno error.
// This is valid only until original URI object is freed or new error happens
extern THREAD_LOCAL struct uri *uri_sub_err_uri;

enum uri_scheme {
	URI_S_HTTP,
	URI_S_HTTPS,
	URI_S_FILE,
	URI_S_DATA,
	URI_S_UNKNOWN,
};

// Create new URI object
// uri: URI string
// parent: parent URI object that should this URI inherit from.
// Returns URI object or NULL on error.
// Possible errors: URI_E_INVALID_URI, URI_E_UNKNOWN_SCHEME
uri_t uri(const char *uri, const uri_t parent) __attribute__((nonnull(1)));

// Free URI object
void uri_free(uri_t) __attribute__((nonnull));

// Returns name of scheme
const char *uri_scheme_string(enum uri_scheme);

// Returns canonical string representation of URI object
const char *uri_uri(const uri_t) __attribute__((nonnull));

// Returns URI scheme
enum uri_scheme uri_scheme(const uri_t) __attribute__((nonnull));

// Check if given URI is local or remote (if downloader is needed or not)
bool uri_is_local(const uri_t) __attribute__((nonnull));

// Returns Unix path from URI. This can be used only on URI of URI_S_FILE type.
// Returned pointer points to malloc allocated memory and should be freed by
// caller.
char *uri_path(const uri_t) __attribute__((nonnull));

// Set output for given URI. Before call of this function nothing is allocated nor
// prepared for data retrieval in URI.
// uri: uri object to register output to
// path: path to file data should be written to
// Returns true on success or false on error.
// Possible errors: URI_E_OUTPUT_OPEN_FAIL
bool uri_output_file(uri_t uri, const char *path) __attribute__((nonnull));

// Set output for given URI. Before call of this function nothing is allocated nor
// prepared for data retrieval in URI.
// uri: uri object to register output to
// path_template: path to file data should be written to
// Returns true on success or false on error.
// Possible errors: URI_E_OUTPUT_OPEN_FAIL
bool uri_output_tmpfile(uri_t uri, char *path_template) __attribute__((nonnull));

// Register given URI to downloader to be downloaded
// uri: URI object downloader is registered to
// downloader: Downloader object
// Returns true on success or false on error.
// Possible errors: URI_E_SIG_FAIL
bool uri_downloader_register(uri_t uri, downloader_t downloader)
	__attribute__((nonnull));

// Provides access to download instance.
// uri: URI object to get download instance for
// Returns download instance or NULL in case uri_downloader_register wasn't called.
download_i_t uri_download_instance(uri_t uri) __attribute__((nonnull));

// Ensure that URI is received and provide access to data.
// You can call this only after any uri_output function.
// For remote ones call this after downloder_register and downloader_run.
// uri: URI object to be finished
// data: pointer where received data will be stored. NULL is set if output was to
//   file or temporally file.
// len: pointer where length of received data will be stored. This is valid only
//   if output is not a file or temporally file.
// Returns true on retrieval success otherwise false.
// Possible errors: URI_E_UNFINISHED_DOWNLOAD, URI_E_DOWNLOAD_FAILED,
// URI_E_OUTPUT_OPEN_FAIL, URI_E_FILE_INPUT_ERROR, URI_E_OUTPUT_WRITE_FAIL,
// URI_E_VERIFY_FAIL, URI_E_SIG_FAIL
bool uri_finish(uri_t uri, const uint8_t **data, size_t *len) __attribute__((nonnull(1)));

// Build error message of URI retrieval failure.
// Returns string with error message.
const char *uri_error_msg(enum uri_error);

// Returns pointer to error string for URI that reported URI_E_DOWNLOAD_FAILED
// when uri_finish was called.
// Returned string is valid until uri object is freed.
const char *uri_download_error(uri_t) __attribute((nonnull));

// HTTPS configurations //
// Set if SSL certification verification should be done
// uri: URI object system CA to be set to
// verify: boolean value setting if verification should or should not be done
// In default this is enabled.
// This setting is inherited.
void uri_set_ssl_verify(uri_t uri, bool verify) __attribute__((nonnull(1)));
// Set certification authority to be used
// uri: URI object CA to be set to
// ca_uri: URI to local CA to be added to list of CAs for SSL verification. You
//   can pass NULL and in such case all URIs are dropped and defaul system SSL
//   certificate bundle is used instead.
// In default system CAs are used.
// This setting is inherited.
// Possible errors: URI_E_NONLOCAL and all errors by uri_to_buffer
bool uri_add_ca(uri_t uri, const char *ca_uri) __attribute__((nonnull(1)));
// Set URI to CRL that is used if CA verification is used
// uri: URI object CRLs to be set to
// crl_uri: URI to local CRL to be added to list of CRLs for SSL verification. You
//   can also pass NULL and in such case all URIs are dropped and CRL verification
//   is disabled.
// In default CRL verification is disabled.
// This setting is inherited.
// Possible errors: URI_E_NONLOCAL and all errors by uri_to_buffer
bool uri_add_crl(uri_t uri, const char *crl_uri) __attribute__((nonnull(1)));
// Set URI OCSP verification
// uri: URI object OCSP to be set to
// enabled: If OCSP should be used
// In default OCSP is enabled.
// This setting is inherited.
void uri_set_ocsp(uri_t uri, bool enabled) __attribute__((nonnull(1)));
// HTTP/HTTPS configuration //
// Set public key verification
// uri: URI object public keys are set to
// pubkey_uri: local URI to public key used to verify sigature. You can pass NULL
//   to drop all added URIs and that way to disable signature verification.
//This setting is inherited.
// Possible errors: URI_E_NONLOCAL and all errors by uri_to_temp_file
bool uri_add_pubkey(uri_t uri, const char *pubkey_uri) __attribute__((nonnull(1)));
// Set URI to signature to be used.
// uri: URI object signature URI to be set to
// sig_uri: string URI to signature. This signature is received with same
//   configuration as given uri. NULL can be passed and in such case is URI used
//   for signature retrieval derived by appending .sig to uri it self. If public
//   key verification is enabled and this function was not called then it is
//   automatically called when URI is registered to downloader or being finished.
// Note that uri created internally to receive this signature has same
// configuration as original uri but all subsequent configuration changes are not
// propagated to internally created uri. This means that you should call this as
// last command of all.
// This option is not inherited!
// Possible errors: all errors by uri_to_temp_file
bool uri_set_sig(uri_t uri, const char *sig_uri) __attribute__((nonnull(1)));

#endif
