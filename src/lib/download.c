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
#include "download.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include "syscnf.h"

// Initial size of storage buffer
#define BUFFER_INIT_SIZE 2048
// User agent reported to server
#define USER_AGENT "Turris Updater/" PACKAGE_VERSION

#define ASSERT_CURL(X) ASSERT((X) == CURLE_OK)
#define ASSERT_CURLM(X) ASSERT((X) == CURLM_OK)

struct downloader {
	struct event_base *ebase; // libevent base
	CURLM *cmulti; // Curl multi instance
	struct event *ctimer; // Timer used by curl

	struct download_i **instances; // Registered instances
	size_t i_size, i_allocated; // instances size and allocated size
	int pending; // Number of still not downloaded instances
	struct download_i *failed; // Latest failed instance (used internally)
};

struct download_i {
	bool done; // If download is finished
	bool success; // If download was successful. Not valid if done is false.
	char error[CURL_ERROR_SIZE]; // error message if download fails

	struct downloader *downloader; // parent downloader
	FILE *output;
	CURL *curl; // easy curl session
	download_pem_t *pems;
};

struct download_pem {
	BIO *cbio;
	STACK_OF(X509_INFO) *info;
};

static void download_check_info(struct downloader *downloader) {
	CURLMsg *msg;
	int msgs_left;
	struct download_i *inst;
	char *url;

	while ((msg = curl_multi_info_read(downloader->cmulti, &msgs_left))) {
		if (msg->msg != CURLMSG_DONE)
			continue; // No other message types are defined in libcurl. We check just because of compatibility with possible future versions.
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &inst));
		ASSERT_CURL(curl_easy_getinfo(msg->easy_handle, CURLINFO_EFFECTIVE_URL, &url));
		inst->done = true;
		if (msg->data.result == CURLE_OK) {
			DBG("Download succesfull (%s)", url);
			inst->success = true;
		} else {
			DBG("Download failed (%s): %s", url, inst->error);
			inst->success = false;
			downloader->failed = inst;
			event_base_loopbreak(downloader->ebase); // break event loop to report error
		}
	}
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
	download_check_info(downloader);
	if (running <= 0 && evtimer_pending(downloader->ctimer, NULL)) { // All transfers are done. Stop timer.
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
	downloader_flush(d);
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

void downloader_flush(struct downloader *d) {
	TRACE("Downloader flush");
	// Instances are freed from back because that prevents data shift in array
	for (int i = d->i_size - 1; i >= 0; i--)
		if (d->instances[i])
			download_i_free(d->instances[i]);
}

void download_opts_def(struct download_opts *opts) {
	opts->timeout = 3600; // one hour
	opts->connect_timeout = 60; // one minute
	opts->follow_redirect = true;
	opts->ssl_verify = true;
	opts->ocsp = true;
	opts->cacert_file = DOWNLOAD_OPT_SYSTEM_CACERT; // In default use system CAs
	opts->capath = DOWNLOAD_OPT_SYSTEM_CAPATH; // In default use compiled in path (system path)
	opts->crl_file = NULL; // In default don't check CRL
	opts->pems = NULL;
}

download_pem_t download_pem(const uint8_t *pem, size_t len) {
	struct download_pem *dpem = malloc(sizeof *dpem);
	dpem->cbio = BIO_new_mem_buf(pem, len);
	if (!dpem->cbio)
		goto error;
	dpem->info = PEM_X509_INFO_read_bio(dpem->cbio, NULL, NULL, NULL);
	if (!dpem->info) {
		BIO_free(dpem->cbio);
		goto error;
	}
	return dpem;

error:
	ERROR("Initializing PEM failed: %s", ERR_error_string(ERR_get_error(), NULL)); 
	free(dpem);
	return NULL;
}

void download_pem_free(download_pem_t dpem) {
	sk_X509_INFO_pop_free(dpem->info, X509_INFO_free);
	BIO_free(dpem->cbio);
	free(dpem);
}

// Called by libcurl to store downloaded data
static size_t download_write_callback(char *ptr, size_t size, size_t nmemb, void *userd) {
	struct download_i *inst = userd;
	size_t rsize = size * nmemb;
	size_t remb = rsize;
	while (remb > 0) {
		ssize_t ds = fwrite(ptr, 1, remb, inst->output);
		if (ds == -1) {
			if (errno == EINTR)
				continue; // interrupted so try again
			char *url;
			ASSERT_CURL(curl_easy_getinfo(inst->curl, CURLINFO_EFFECTIVE_URL, &url));
			ERROR("(%s) Data write failed: %s", url, strerror(errno));
			return 0; // value other then rsize signals write error to libcurl
		}
		remb -= (size_t)ds;
	}
	return rsize;
}

static CURLcode download_sslctx(CURL *curl __attribute__((unused)), void *sslctx, void *parm) {
	struct download_pem **pems = parm;
	X509_STORE *cts = SSL_CTX_get_cert_store((SSL_CTX *)sslctx);
	if (!cts) {
		TRACE("Failed to get cert store: %s", ERR_error_string(ERR_get_error(), NULL));
		return CURLE_ABORTED_BY_CALLBACK;
	}

	while (*pems) {
		for (int i = 0; i < sk_X509_INFO_num((*pems)->info); i++) {
			X509_INFO *itmp = sk_X509_INFO_value((*pems)->info, i);
			if(itmp->x509)
				X509_STORE_add_cert(cts, itmp->x509);
			if(itmp->crl)
				X509_STORE_add_crl(cts, itmp->crl);
		}
		pems++;
	}
	return CURLE_OK;
}

static download_pem_t *pemsdup(const download_pem_t *pem) {
	size_t len = 0;
	while (pem[len++]);
	download_pem_t *npem = malloc(len * sizeof *npem);
	return memcpy(npem, pem, len * sizeof *pem);
}

struct download_i *download(struct downloader *downloader, const char *url,
		FILE *output, const struct download_opts *opts) {
	struct download_i *inst = malloc(sizeof *inst);
	inst->output = output;
	TRACE("Download url: %s", url);
	// TODO TRACE configured options
	inst->done = false;
	inst->success = false;
	inst->downloader = downloader;
	inst->pems = NULL;

	inst->curl = curl_easy_init();
	ASSERT_MSG(inst->curl, "Curl download instance creation failed");
#define CURL_SETOPT(OPT, VAL) ASSERT_CURL(curl_easy_setopt(inst->curl, OPT, VAL))
	CURL_SETOPT(CURLOPT_URL, url);
	CURL_SETOPT(CURLOPT_ACCEPT_ENCODING, ""); // Enable all supported built-in compressions
	CURL_SETOPT(CURLOPT_FOLLOWLOCATION, opts->follow_redirect); // Follow redirects
	CURL_SETOPT(CURLOPT_TIMEOUT, opts->timeout);
	CURL_SETOPT(CURLOPT_CONNECTTIMEOUT, opts->connect_timeout);
	CURL_SETOPT(CURLOPT_FAILONERROR, 1); // If we use http and request fails (response >= 400) request also fails. TODO according to documentation this doesn't cover authentications errors. If authentication is added, this won't be enough.
	char *user_agent;
	if (root_dir_is_root())
		user_agent = aprintf(USER_AGENT " (%s)", os_release(OS_RELEASE_PRETTY_NAME));
	else
		user_agent = aprintf(USER_AGENT " (%s; %s)",
			host_os_release(OS_RELEASE_PRETTY_NAME), os_release(OS_RELEASE_PRETTY_NAME));
	CURL_SETOPT(CURLOPT_USERAGENT, user_agent); // We set our own User Agent, so our server knows we're not just some bot
	if (opts->ssl_verify) {
		if (opts->cacert_file != DOWNLOAD_OPT_SYSTEM_CACERT)
			CURL_SETOPT(CURLOPT_CAINFO, opts->cacert_file);
		if (opts->capath != DOWNLOAD_OPT_SYSTEM_CAPATH)
			CURL_SETOPT(CURLOPT_CAPATH, opts->capath);
		if (opts->crl_file)
			CURL_SETOPT(CURLOPT_CRLFILE, opts->crl_file);
		if (opts->pems) {
			inst->pems = pemsdup(opts->pems);
			CURL_SETOPT(CURLOPT_SSL_CTX_FUNCTION, download_sslctx);
			CURL_SETOPT(CURLOPT_SSL_CTX_DATA, inst->pems);
		}
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

void download_i_free(struct download_i *inst) {
	TRACE("Downloader: free instance");
	// Remove instance from downloader
	int i = inst->downloader->i_size - 1;
	while (i >= 0 && inst->downloader->instances[i] != inst)
		i--;
	ASSERT_MSG(i >= 0, "Download instance is not registered with downloader that it specifies");
	inst->downloader->i_size--;
	memmove(inst->downloader->instances + i, inst->downloader->instances + i + 1,
			(inst->downloader->i_size - i) * sizeof *inst->downloader->instances);

	// Free instance it self
	ASSERT_CURLM(curl_multi_remove_handle(inst->downloader->cmulti, inst->curl)); // remove download from multi handler
	curl_easy_cleanup(inst->curl); // and clean download (also closing running connection)
	if (inst->pems)
		free(inst->pems);
	free(inst);
}

bool download_is_done(download_i_t inst) {
	return inst->done;
}

bool download_is_success(download_i_t inst) {
	return inst->success;
}

const char *download_error(download_i_t inst) {
	return inst->error;
}
