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
#include "download.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

// Initial size of storage buffer
#define BUFFER_INIT_SIZE 2048
// User agent reported to server
#define USER_AGENT ( "Turris Updater/" UPDATER_VERSION )

#define ASSERT_CURL(X) ASSERT((X) == CURLE_OK)
#define ASSERT_CURLM(X) ASSERT((X) == CURLM_OK)

static bool download_check_info(struct downloader *downloader) {
	CURLMsg *msg;
	int msgs_left;
	struct download_i *inst;
	char *url;
	bool new_handle = false;

	while ((msg = curl_multi_info_read(downloader->cmulti, &msgs_left))) {
		if (msg->msg != CURLMSG_DONE)
			continue; // No other message types are defined in libcurl. We check just because of compatibility with possible future versions.
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &inst));
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &url));
		if (msg->data.result == CURLE_OK) {
			DBG("Download succesfull (%s)", url);
			inst->success = true;
			inst->done = true;
		} else if (inst->retries > 1) { // retry download
			DBG("Download failed, trying again %d (%s): %s", inst->retries, url, inst->error);
			inst->curl = curl_easy_duphandle(msg->easy_handle);
			ASSERT_CURLM(curl_multi_remove_handle(downloader->cmulti, msg->easy_handle));
			curl_easy_cleanup(msg->easy_handle);
			ASSERT_CURLM(curl_multi_add_handle(downloader->cmulti, inst->curl));
			inst->retries--;
			new_handle = true;
			// TODO autodrop!
		} else {
			DBG("Download failed (%s): %s", url, inst->error);
			inst->success = false;
			inst->done = true;
			downloader->failed = inst;
			event_base_loopbreak(downloader->ebase); // break event loop to report error
		}
	}
	return new_handle;
}

struct download_socket_data {
	struct downloader *downloader;
	struct event *ev;
};

// Event callback on action
static void download_event_cb(int fd, short kind, void *userp) {
	struct download_socket_data *sdata = userp;
	int action = ((kind & EV_READ) ? CURL_CSELECT_IN : 0) | ((kind & EV_WRITE) ? CURL_CSELECT_OUT : 0);
	int running = 0;
	struct downloader *downloader = sdata->downloader; // in curl_multi_socket_action sdata can be freed so we can't expect it to exist after it
	ASSERT_CURLM(curl_multi_socket_action(downloader->cmulti, fd, action, &running)); // curl do
	bool new_handle = download_check_info(downloader);
	if (!new_handle && running <= 0 && evtimer_pending(downloader->ctimer, NULL)) { // All transfers are done. Stop timer.
		evtimer_del(downloader->ctimer);
	}
}

// Curl callback to set watched sockets
static int download_socket_cb(CURL *curl_easy, curl_socket_t s, int what, void *userp, void *socketp) {
	struct download_i *inst;
	ASSERT_CURL(curl_easy_getinfo(curl_easy, CURLINFO_PRIVATE, &inst));
	struct downloader *downloader = userp;
	struct download_socket_data *sdata = socketp;
	if (what == CURL_POLL_REMOVE) {
		event_free(sdata->ev);
		free(sdata);
	} else {
		if (!sdata) { // New socket. No data associated.
			sdata = malloc(sizeof *sdata);
			*sdata = (struct download_socket_data) {
				.downloader = downloader,
				.ev = NULL,
			};
			sdata->ev = 0;
			ASSERT_CURLM(curl_multi_assign(downloader->cmulti, s, sdata));
		}
		short kind = ((what & CURL_POLL_IN) ? EV_READ : 0) | ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST;
		if (sdata->ev) {
			event_del(sdata->ev);
			event_assign(sdata->ev, downloader->ebase, s, kind, download_event_cb, sdata);
		} else
			sdata->ev = event_new(downloader->ebase, s, kind, download_event_cb, sdata);
		event_add(sdata->ev, NULL);
	}
	return 0;
}

// Curl callback to set timer
static int download_timer_set(CURLM *cmulti __attribute__((unused)), long timeout_ms, void *userp) {
	struct downloader *downloader = userp;
	struct timeval timeout;
	timeout.tv_sec = timeout_ms / 1000;
	timeout.tv_usec = (timeout_ms % 1000) * 1000;
	evtimer_add(downloader->ctimer, &timeout);
	return 0;
}

// Event timer called on timer configured by curl timer callback
static void download_timer_cb(int fd __attribute__((unused)), short kind __attribute__((unused)), void *userp) {
	struct downloader *downloader = userp;
	int running = 0;
	ASSERT_CURLM(curl_multi_socket_action(downloader->cmulti, CURL_SOCKET_TIMEOUT, 0, &running));
	download_check_info(downloader);
}

struct downloader *downloader_new(int parallel) {
	TRACE("Downloader allocation");
	struct downloader *d = malloc(sizeof *d);

	struct event_config *econfig = event_config_new();
	event_config_set_flag(econfig, EVENT_BASE_FLAG_NOLOCK); // We don't have threads
	d->ebase = event_base_new_with_config(econfig);
	ASSERT_MSG(d->ebase, "Failed to allocate the libevent event loop");
	event_config_free(econfig);

	ASSERT_MSG(!curl_global_init(CURL_GLOBAL_SSL), "Curl initialization failed");
	ASSERT(d->cmulti = curl_multi_init());
#define CURLM_SETOPT(OPT, VAL) ASSERT_CURLM(curl_multi_setopt(d->cmulti, OPT, VAL))
	CURLM_SETOPT(CURLMOPT_MAX_TOTAL_CONNECTIONS, parallel);
	CURLM_SETOPT(CURLMOPT_SOCKETFUNCTION, download_socket_cb);
	CURLM_SETOPT(CURLMOPT_SOCKETDATA, d);
	CURLM_SETOPT(CURLMOPT_TIMERFUNCTION, download_timer_set);
	CURLM_SETOPT(CURLMOPT_TIMERDATA, d);
#undef CURLM_SETOPT
	d->ctimer = evtimer_new(d->ebase, download_timer_cb, d);

	d->i_size = 0;
	d->i_allocated = 1;
	d->instances = malloc(d->i_allocated * sizeof *d->instances);
	d->pending = 0;
	d->failed = NULL;
	return d;
}

void downloader_free(struct downloader *d) {
	TRACE("Downloader free");
	// Instances are freed from back because that prevents data shift in array
	for (int i = d->i_size - 1; i >= 0; i--)
		if (d->instances[i])
			download_i_free(d->instances[i]);
	free(d->instances);
	event_free(d->ctimer);
	curl_multi_cleanup(d->cmulti);
	curl_global_cleanup(); // We call this for every curl_global_init call.
	event_base_free(d->ebase);
	free(d);
}

struct download_i *downloader_run(struct downloader *downloader) {
	TRACE("Downloader run");
	event_base_dispatch(downloader->ebase);
	if (downloader->failed) {
		struct download_i *inst = downloader->failed;
		downloader->failed = NULL;
		return inst;
	}
	return NULL;
}

void download_opts_def(struct download_opts *opts) {
	opts->timeout = 120; // 2 minutes
	opts->connect_timeout = 30; // haf of a minute
	opts->retries = 3;
	opts->follow_redirect = true;
	opts->ssl_verify = true;
	opts->ocsp = true;
	opts->cacert_file = NULL; // In default use system CAs
	opts->crl_file = NULL; // In default don't check CRL
}

// Called by libcurl to store downloaded data
static size_t download_write_callback(char *ptr, size_t size, size_t nmemb, void *userd) {
	struct download_i *inst = userd;
	size_t rsize = size * nmemb;
	switch (inst->out_t) {
		case DOWN_OUT_T_FILE: {
			size_t remb = rsize;
			while (remb > 0) {
				size_t ds = write(inst->out.fd, ptr, remb);
				if (ds == (size_t)-1) {
					if (errno == EINTR)
						continue; // interrupted so try again
					else {
						char *url;
						ASSERT_CURL(curl_easy_getinfo(inst->curl, CURLINFO_EFFECTIVE_URL, &url));
						ERROR("(%s) Data write to file failed: %s", url, strerror(errno));
						return 0; // value other then rsize signals write error to libcurl
					}
				}
				remb -= ds;
			}
			break;
			}
		case DOWN_OUT_T_BUFFER:
			inst->out.buff->data = realloc(inst->out.buff->data, inst->out.buff->size + rsize + 1);
			memcpy(inst->out.buff->data + inst->out.buff->size, ptr, rsize);
			inst->out.buff->size += rsize;
			inst->out.buff->data[inst->out.buff->size] = '\0';
			break;
	}
	return rsize;
}

static struct download_i *new_instance(struct downloader *downloader,
		const char *url, const char *output_path, const struct download_opts *opts,
		bool autodrop, enum download_output_type type) {
	// TODO TRACE configured options
	struct download_i *inst = malloc(sizeof *inst);
	switch (type) {
		case DOWN_OUT_T_FILE:
			// Note: For some reason umask seems to be sometime changed. So we set here our own explicitly.
			inst->out.fd = open(output_path, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
			if (inst->out.fd == -1) {
				ERROR("(%s) Opening output file \"%s\" failed: %s", url, output_path, strerror(errno));
				free(inst);
				return NULL;
			}
			break;
		case DOWN_OUT_T_BUFFER:
			inst->out.buff = malloc(sizeof *inst->out.buff);
			inst->out.buff->size = 0;
			inst->out.buff->data = NULL;
			break;
	}
	inst->done = false;
	inst->success = false;
	inst->autodrop = autodrop;
	inst->retries = opts->retries;
	inst->downloader = downloader;
	inst->out_t = type;

	inst->curl = curl_easy_init();
	ASSERT_MSG(inst->curl, "Curl download instance creation failed");
#define CURL_SETOPT(OPT, VAL) ASSERT_CURL(curl_easy_setopt(inst->curl, OPT, VAL))
	CURL_SETOPT(CURLOPT_URL, url);
	CURL_SETOPT(CURLOPT_ACCEPT_ENCODING, ""); // Enable all supported built-in compressions
	CURL_SETOPT(CURLOPT_FOLLOWLOCATION, opts->follow_redirect); // Follow redirects
	CURL_SETOPT(CURLOPT_TIMEOUT, opts->timeout);
	CURL_SETOPT(CURLOPT_CONNECTTIMEOUT, opts->connect_timeout);
	CURL_SETOPT(CURLOPT_FAILONERROR, 1); // If we use http and request fails (response >= 400) request also fails. TODO according to documentation this doesn't cover authentications errors. If authentication is added, this won't be enough.
	CURL_SETOPT(CURLOPT_USERAGENT, USER_AGENT); // We set our own User Agent, so our server knows we're not just some bot
	if (opts->ssl_verify) {
		if (opts->cacert_file)
			CURL_SETOPT(CURLOPT_CAINFO, opts->cacert_file);
		if (opts->crl_file)
			CURL_SETOPT(CURLOPT_CRLFILE, opts->crl_file);
		CURL_SETOPT(CURLOPT_SSL_VERIFYSTATUS, opts->ocsp);
	} else
		CURL_SETOPT(CURLOPT_SSL_VERIFYPEER, 0L);
	CURL_SETOPT(CURLOPT_WRITEFUNCTION, download_write_callback);
	CURL_SETOPT(CURLOPT_WRITEDATA, inst);
	CURL_SETOPT(CURLOPT_ERRORBUFFER, inst->error);
	CURL_SETOPT(CURLOPT_PRIVATE, inst);
	// TODO We might set XFERINFOFUNCTION here to use it for reporting progress of download to user.
#undef CURL_SETOPT
	ASSERT_CURLM(curl_multi_add_handle(downloader->cmulti, inst->curl));

	// Add instance to downloader
	if (downloader->i_size >= downloader->i_allocated) {
		downloader->i_allocated *= 2;
		downloader->instances = realloc(downloader->instances,
				downloader->i_allocated * sizeof *downloader->instances);
	}
	downloader->instances[downloader->i_size++] = inst;

	return inst;
}

struct download_i *download_file(struct downloader *downloader, const char *url,
		const char *output_path, bool autodrop, const struct download_opts *opts) {
	TRACE("Downloder: url %s to file %s", url, output_path);
	return new_instance(downloader, url, output_path, opts, autodrop, DOWN_OUT_T_FILE);
}

struct download_i *download_data(struct downloader *downloader, const char *url,
		const struct download_opts *opts) {
	TRACE("Downloder: url %s", url);
	return new_instance(downloader, url, NULL, opts, false, DOWN_OUT_T_BUFFER);
}

void download_i_free(struct download_i *inst) {
	TRACE("Downloader: free instance");
	// Remove instance from downloader
	int i = inst->downloader->i_size - 1;
	while (i >= 0 && inst->downloader->instances[i] != inst)
		i--;
	ASSERT_MSG(i >= 0, "Download instance is not registered with downloader that it specifies");
	memmove(inst->downloader->instances + i, inst->downloader + i + 1,
			(inst->downloader->i_size - i - 1) * sizeof *inst->downloader->instances);
	inst->downloader->i_size--;

	// Free instance it self
	ASSERT_CURLM(curl_multi_remove_handle(inst->downloader->cmulti, inst->curl)); // remove download from multi handler
	curl_easy_cleanup(inst->curl); // and clean download (also closing running connection)
	switch (inst->out_t) {
		case DOWN_OUT_T_FILE:
			close(inst->out.fd);
			break;
		case DOWN_OUT_T_BUFFER:
			if (inst->out.buff->data)
				free(inst->out.buff->data);
			free(inst->out.buff);
			break;
	}
	free(inst);
}
