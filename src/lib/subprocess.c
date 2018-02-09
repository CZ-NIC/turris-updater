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

#define _GNU_SOURCE
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
#include "logging.h"

static int kill_timeout = 3;

static void run_child(const char *cmd, const char *args[], int p_out[2], int p_err[2]) {
	// Close unneded FDs
	ASSERT(close(0) != -1);
	ASSERT(close(p_out[0]) != -1);
	ASSERT(dup2(p_out[1], 1) != -1 && close(p_out[1]) != -1);
	ASSERT(close(p_err[0]) != -1);
	ASSERT(dup2(p_err[1], 2) != -1 && close(p_err[1]) != -1);
	// Exec
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

int subprocl(int timeout, const char *cmd, const char *args[]) {
	FILE *fds[2] = {stdout, stderr};
	return subproclo(timeout, fds, cmd, args);
}

int subproclo(int timeout, FILE *fd[2], const char *cmd, const char *args[]) {
	// Prepare pipes for stdout and stderr
	int p_err[2], p_out[2];
	pipe2(p_err, O_NONBLOCK);
	pipe2(p_out, O_NONBLOCK);

	// Fork
	pid_t pid = fork();
	if (pid == -1)
		DIE("Failed to fork command %s: %s", cmd, strerror(errno));
	else if (pid == 0)
		run_child(cmd, args, p_out, p_err);

	ASSERT(close(p_out[1]) != -1);
	ASSERT(close(p_err[1]) != -1);

	struct pollfd pfds[] = {
		{ .fd = p_out[0], .events = POLLIN },
		{ .fd = p_err[0], .events = POLLIN }
	};
	time_t t_start = time(NULL);
	bool term_sent = false;
	int dead = 0, i;
	while (dead < 2) {
		time_t rem_t = timeout - time(NULL) + t_start;
		ASSERT(poll(pfds, 2, rem_t < 0 ? 0 : rem_t) != -1);
		dead = 0;
		for (i = 0; i < 2; i++) {
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
		if (timeout >= 0 && (time(NULL) - t_start) >= timeout) {
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
	va_end(args);
	argv[argc - 1] = NULL;
	return subproclo(timeout, fd, cmd, argv);
}

void subproc_kill_t(int timeout) {
	kill_timeout = timeout;
}
