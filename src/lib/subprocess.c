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
#include "subprocess.h"

#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sys/select.h>
#include <time.h>
#include <sys/wait.h>

static int kill_timeout = 60000;

void subproc_kill_t(int timeout) {
	kill_timeout = timeout;
}

static void run_child(const char *cmd, const char *args[], subproc_callback callback, void *data, int p_out[2], int p_err[2]) {
	// Close unneded FDs
	ASSERT(close(0) != -1);
	ASSERT(close(p_out[0]) != -1);
	ASSERT(dup2(p_out[1], 1) != -1 && close(p_out[1]) != -1);
	ASSERT(close(p_err[0]) != -1);
	ASSERT(dup2(p_err[1], 2) != -1 && close(p_err[1]) != -1);
	// Callback
	if (callback)
		callback(data);
	// Exec
	if (cmd) {
		size_t arg_c = 2; // cmd and NULL terminator
		for (const char **p = args; *p; p++)
			 arg_c++;
		char *argv[arg_c];
		size_t i = 1;
		for (const char **p = args; *p; p++)
			argv[i++] = strdup(*p);
		argv[i] = NULL;
		argv[0] = strdup(cmd);
		execvp(cmd, argv);
		DIE("Failed to exec %s: %s", cmd, strerror(errno));
	} else
		exit(0); // We just exit child
}

int subprocv(int timeout, const char *cmd, ...) {
	va_list va_args;
	va_start(va_args, cmd);
	int res = vsubprocv(timeout, cmd, va_args);
	va_end(va_args);
	return res;
}

int subprocvo(int timeout, FILE *fd[2], const char *cmd, ...) {
	va_list va_args;
	va_start(va_args, cmd);
	int res = vsubprocvo(timeout, fd, cmd, va_args);
	va_end(va_args);
	return res;
}

int subprocvoc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *cmd, ...) {
	va_list va_args;
	va_start(va_args, cmd);
	int res = vsubprocvoc(timeout, fd, callback, data, cmd, va_args);
	va_end(va_args);
	return res;
}

int subprocl(int timeout, const char *cmd, const char *args[]) {
	FILE *fds[2] = {stdout, stderr};
	return subproclo(timeout, fds, cmd, args);
}

int subproclo(int timeout, FILE *fd[2], const char *cmd, const char *args[]) {
	return subprocloc(timeout, fd, NULL, NULL, cmd, args);
}

int subprocloc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *cmd, const char *args[]) {
	struct log_buffer log;
	log_buffer_init(&log, LL_TRACE);
	if (log.f) {
		fprintf(log.f, "Running subprocess: %s", cmd);
		for (const char **p = args; *p; p++)
			fprintf(log.f, " %s", *p);
		fclose(log.f);
		TRACE("%s", log.char_buffer);
		free(log.char_buffer);
	}
	// Prepare pipes for stdout and stderr
	int p_err[2], p_out[2];
	pipe2(p_err, O_NONBLOCK);
	pipe2(p_out, O_NONBLOCK);

	// Fork
	pid_t pid = fork();
	if (pid == -1)
		DIE("Failed to fork command %s: %s", cmd, strerror(errno));
	else if (pid == 0)
		run_child(cmd, args, callback, data, p_out, p_err);

	ASSERT(close(p_out[1]) != -1);
	ASSERT(close(p_err[1]) != -1);

	struct pollfd pfds[] = {
		{ .fd = p_out[0], .events = POLLIN },
		{ .fd = p_err[0], .events = POLLIN }
	};
	time_t t_start = time(NULL);
	bool term_sent = false;
	while (true) {
		int poll_timeout = -1;
		if (timeout >= 0) {
			int rem_t = timeout - 1000*(time(NULL) - t_start);
			poll_timeout = rem_t < 0 ? 0 : rem_t;
		}
		// We ignore interrupt errors as those are really not an errors
		ASSERT_MSG(poll(pfds, 2, poll_timeout) != -1 || errno == EINTR, "Subprocess poll failed with error: %s", strerror(errno));
		int dead = 0;
		for (int i = 0; i < 2; i++) {
			if (pfds[i].revents & POLLIN) {
				char *buff[64];
				ssize_t loaded;
				while ((loaded = read(pfds[i].fd, buff, 64)) > 0)
					fwrite(buff, sizeof(char), loaded, fd[i]);
			}
			if (pfds[i].revents & POLLHUP)
				dead++;
			ASSERT(!(pfds[i].revents & POLLERR) && !(pfds[i].revents & POLLNVAL));
		}
		if (dead >= 2)
			break; // Both feeds are dead so break this loop
		if (timeout >= 0 && 1000*(time(NULL) - t_start) >= timeout) {
			if (term_sent) { // Send SIGKILL
				ASSERT(kill(pid, SIGKILL) != -1);
				break;
			} else { // Send SIGTERM and extend timeout
				ASSERT(kill(pid, SIGTERM) != -1);
				timeout += kill_timeout;
				term_sent = true;
			}
		}
	}

	ASSERT(close(p_out[0]) != -1);
	ASSERT(close(p_err[0]) != -1);

	int wstatus;
	ASSERT(waitpid(pid, &wstatus, 0) != -1);
	return wstatus;
}

int vsubprocv(int timeout, const char *cmd, va_list args) {
	FILE *fds[2] = {stdout, stderr};
	return vsubprocvo(timeout, fds, cmd, args);
}

int vsubprocvo(int timeout, FILE *fd[2], const char *cmd, va_list args) {
	return vsubprocvoc(timeout, fd, NULL, NULL, cmd, args);
}

int vsubprocvoc(int timeout, FILE *fd[2], subproc_callback callback, void *data, const char *cmd, va_list args) {
	size_t argc = 1; // For NULL terminator
	// Count (use copy for that)
	va_list va_copy;
	va_copy(va_copy, args);
	while (va_arg(va_copy, const char *) != NULL)
		argc++;
	va_end(va_copy);
	// Copy to array
	const char *argv[argc];
	size_t i = 0;
	while((argv[i++] = va_arg(args, const char *)) != NULL);
	argv[argc - 1] = NULL;
	return subprocloc(timeout, fd, callback, data, cmd, argv);
}

int lsubprocv(enum log_subproc_type type, const char *message, char **output, int timeout, const char *cmd, ...) {
	va_list va_args;
	va_start(va_args, cmd);
	int ec = lvsubprocv(type, message, output, timeout, cmd, va_args);
	va_end(va_args);
	return ec;
}

int lsubprocvc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *cmd, ...) {
	va_list va_args;
	va_start(va_args, cmd);
	int ec = lvsubprocvc(type, message, output, timeout, callback, data, cmd, va_args);
	va_end(va_args);
	return ec;
}

int lsubprocl(enum log_subproc_type type, const char *message, char **output, int timeout, const char *cmd, const char *args[]) {
	return lsubproclc(type, message, output, timeout, NULL, NULL, cmd, args);
}

int lsubproclc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *cmd, const char *args[]) {
	struct log_subproc lsp;
	log_subproc_open(&lsp, type, message);
	FILE *fds[] = {lsp.out, lsp.err};
	int ec = subprocloc(timeout, fds, callback, data, cmd, args);
	log_subproc_close(&lsp, output);
	return ec;
}

int lvsubprocv(enum log_subproc_type type, const char *message, char **output, int timeout, const char *cmd, va_list args) {
	return lsubprocvc(type, message, output, timeout, NULL, NULL, cmd, args);
}

int lvsubprocvc(enum log_subproc_type type, const char *message, char **output, int timeout, subproc_callback callback, void *data, const char *cmd, va_list args) {
	struct log_subproc lsp;
	log_subproc_open(&lsp, type, message);
	FILE *fds[] = {lsp.out, lsp.err};
	int ec = vsubprocvoc(timeout, fds, callback, data, cmd, args);
	log_subproc_close(&lsp, output);
	return ec;
}
