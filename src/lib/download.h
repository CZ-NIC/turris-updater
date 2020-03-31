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

struct download_i;

// Download manager object
struct downloader {
	struct event_base *ebase; // libevent base
	CURLM *cmulti; // Curl multi instance
	struct event *ctimer; // Timer used by curl

	struct download_i **instances; // Registered instances
	size_t i_size, i_allocated; // instances size and allocated size
	int pending; // Number of still not downloaded instances
	struct download_i *failed; // Latest failed instance (used internally)
};

// Download options (additional options configuring security and more)
struct download_opts {
	long timeout; // Download timeout (including download retries)
	long connect_timeout; // Timeout for single connection
	int retries; // Number of full download retries
	bool follow_redirect; // If HTTP request 3xx should be followed
	bool ssl_verify; // If SSL should be verified
	bool ocsp; // If OCSP should be used for certificate verification
	const char *cacert_file; // Path to custom CA certificate bundle
	const char *capath; // Path to directory containing CA certificates
	const char *crl_file; // Path to custom CA crl
};

// Download instance. Identifier of single download.
struct download_i {
	bool done; // If download is finished
	bool success; // If download was successful. Not valid if done is false.
	char error[CURL_ERROR_SIZE]; // error message if download fails
	int retries; // Number of reties we have
	struct downloader *downloader; // parent downloader

	FILE *output;

	CURL *curl; // easy curl session
};


// Initialize new download manager
// parallel: Number of possible parallel downloadings
// Returns new instance of downloader
struct downloader *downloader_new(int parallel);

// Free given instance of downloader
void downloader_free(struct downloader*) __attribute__((nonnull));

// Run downloader and download all registered URLs
// return: NULL on success otherwise pointer to download instance that failed.
struct download_i *downloader_run(struct downloader*) __attribute__((nonnull));

// Remove all download instances from downloader
void downloader_flush(struct downloader*) __attribute__((nonnull));

// Set default values for download_opts
// opts: Allocated instance of download options to be set to defaults
// Note: strings in download_opts are set to NULL and previous values are NOT
// freed.
void download_opts_def(struct download_opts *opts) __attribute__((nonnull));

// Register given URL to be downloaded.
// downloader: 
// url: URL data are downloaded from
// opts: Download options (does not have to exist during instance existence)
// file: FILE pointer in which received data will be written
// Returns download instance
struct download_i *download(struct downloader *downloader, const char *url,
		FILE *output, const struct download_opts *opts)
	__attribute__((nonnull(1, 2, 3, 4)));

// Free download instance
void download_i_free(struct download_i*) __attribute__((nonnull));

#endif
