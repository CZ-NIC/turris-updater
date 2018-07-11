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
#ifndef UPDATER_DOWNLOAD_H
#define UPDATER_DOWNLOAD_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <event2/event.h>
#include <curl/curl.h>
#include <lua.h>
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

enum download_output_type {
	DOWN_OUT_T_FILE,
	DOWN_OUT_T_BUFFER
};

// Download instance. Identifier of single download.
struct download_i {
	bool done; // What ever is download finished
	bool success; // If download was successful. Not valid if done is false.
	bool autodrop; // if true then instance is freed immediately after completion
	char error[CURL_ERROR_SIZE]; // error message if download fails
	int retries; // Number of reties we have
	struct downloader *downloader; // parent downloader

	enum download_output_type out_t; // What output this instance utilizes
	union {
		int fd; // Used when writing to file
		struct {
			uint8_t *data; // Buffer for output data
			size_t size; // Amount of downloaded data
		} *buff; // Used when writing to buffer
	} out; // Output data

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

// Set default values for download_opts
// opts: Allocated instance of download options to be set to defaults
// Note: strings in download_opts are set to NULL and previous values are NOT
// freed.
void download_opts_def(struct download_opts *opts) __attribute__((nonnull));

// Register given URL to be downloaded to file.
// url: URL data are downloaded from
// output_path: Path where data are going to be stored (written to)
// opts: Download options
// Returns download instance
struct download_i *download_file(struct downloader *downloader, const char *url,
		const char *output_path, bool autodrop, const struct download_opts *opts)
		__attribute__((nonnull(1, 2, 3, 5)));

// Register given URL to be downloaded to internal buffer.
// url: URL data are downloaded from
// opts: Download options
// Returns download instance
struct download_i *download_data(struct downloader *downloader, const char *url,
		const struct download_opts *opts) __attribute__((nonnull(1, 2, 3)));

// Free download instance
void download_i_free(struct download_i*) __attribute__((nonnull));


// Create the downloader module and inject it into the lua state
void downloader_mod_init(lua_State *L) __attribute__((nonnull));

#endif
