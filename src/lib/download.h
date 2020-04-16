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
#ifndef UPDATER_DOWNLOAD_H
#define UPDATER_DOWNLOAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <event2/event.h>
#include <curl/curl.h>
#include "logging.h"

// Download manager object
struct downloader;
typedef struct downloader* downloader_t;

// Download instance. Identifier of single download.
struct download_i;
typedef struct download_i* download_i_t;

// PEM used for CA certificates and CRLs
struct download_pem;
typedef struct download_pem* download_pem_t;

#define DOWNLOAD_OPT_SYSTEM_CACERT ((const char*)-1)
#define DOWNLOAD_OPT_SYSTEM_CAPATH ((const char*)-1)

// Download options (additional options configuring security and more)
struct download_opts {
	long timeout; // Download timeout
	long connect_timeout; // Timeout for single connection
	bool follow_redirect; // If HTTP request 3xx should be followed
	bool ssl_verify; // If SSL should be verified
	bool ocsp; // If OCSP should be used for certificate verification
	const char *cacert_file; // Path to custom CA certificate bundle
	const char *capath; // Path to directory containing CA certificates
	const char *crl_file; // Path to custom CA crl
	const download_pem_t *pems; // NULL terminated array of PEM certificates
};


// Initialize new download manager
// parallel: Number of possible parallel downloadings
// Returns new instance of downloader
downloader_t downloader_new(int parallel) __attribute__((malloc));

// Free given instance of downloader
void downloader_free(downloader_t) __attribute__((nonnull));

// Run downloader and download all registered URLs
// return: NULL on success otherwise pointer to download instance that failed.
download_i_t downloader_run(downloader_t) __attribute__((nonnull));

// Remove all download instances from downloader
void downloader_flush(downloader_t) __attribute__((nonnull));

// Set default values for download_opts
// opts: Instance of download options to be set to defaults
// Note: strings and arrays in download_opts are set to NULL and previous values
//   are NOT freed.
void download_opts_def(struct download_opts *opts) __attribute__((nonnull));

// Initialize/load certificate in PEM format
// pem: data with certificate in PEM format
// len: size of data
// Returns new download_pem_t instance or NULL on error.
download_pem_t download_pem(const uint8_t *pem, size_t len)
	__attribute__((nonnull(1),malloc));

// Free download_pem_t instance
void download_pem_free(download_pem_t) __attribute__((nonnull));

// Register given URL to be downloaded.
// downloader: downloader instance to register download to
// url: URL data are downloaded from
// opts: Download options (does not have to exist during instance existence)
// file: FILE pointer in which received data will be written
// Returns download instance.
download_i_t download(downloader_t, const char *url, FILE *output,
		const struct download_opts *opts) __attribute__((nonnull,malloc));

// Free download instance
void download_i_free(download_i_t) __attribute__((nonnull));

// Check if given instance is completed (processed by downloader)
// Returns true if completed and false otherwise.
bool download_is_done(download_i_t) __attribute__((nonnull));

// Check if given instance completed with success.
// Returns true if completed and false if it failed. You can use download_error to
// get error message. Returned value is only valid if download_is_done returns
// true.
bool download_is_success(download_i_t) __attribute__((nonnull));

// Returns string with error message desciring failure reason.
// Returned string is only valid if download_is_success returns false and is valid
// till instance is not freed.
const char *download_error(download_i_t) __attribute__((nonnull));

#endif
