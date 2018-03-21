# Copyright (c) 2018, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the CZ.NIC nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL CZ.NIC BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""This module is core of udpdater-supervisor. It runs and supervise updater
execution.
"""
from __future__ import print_function
import os
import sys
import subprocess
import atexit
import signal
from threading import Thread
from .utils import setup_alarm, report
from .const import PKGUPDATE_CMD, PKGUPDATE_TIMEOUT, PKGUPDATE_TIMEOUT_KILL
from ._pidlock import PidLock


class Supervisor:
    "pkgupdate supervisor"
    def __init__(self, verbose):
        self.verbose = verbose
        self.kill_timeout = 0
        self.process = None
        self._devnull = open(os.devnull, 'w')
        self._stdout_thread = Thread(
            target=self._stdout,
            name="pkgupdate-stdout")
        self._stderr_thread = Thread(
            target=self._stderr,
            name="pkgupdate-stderr")
        atexit.register(self._at_exit)

    def run(self):
        "Run pkgupdate"
        if self.process is not None:
            raise Exception("Only one call to Supervisor.run is allowed.")
        self.process = subprocess.Popen(
            PKGUPDATE_CMD,
            stdin=self._devnull,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        self._stdout_thread.start()
        self._stderr_thread.start()

    def join(self, timeout, killtimeout):
        "Join pkgupdate execution and return exit code."
        self.kill_timeout = killtimeout
        # Wait for pkgupdate to exit (with timeout)
        setup_alarm(self._timeout, timeout)
        exit_code = self.process.wait()
        signal.alarm(0)
        # Wait untill we process all output
        self._stdout_thread.join()
        self._stderr_thread.join()
        # Dump process
        self.process = None
        # Return exit code
        return exit_code

    def _stdout(self):
        # TODO record block to report failures
        while True:
            line = self.process.stdout.readline()
            if not line:
                break
            if self.verbose:
                print(line.decode(sys.getdefaultencoding()), end='')
                sys.stdout.flush()

    def _stderr(self):
        # TODO record block to report failures
        while True:
            line = self.process.stderr.readline()
            if not line:
                break
            if self.verbose:
                print(line.decode(sys.getdefaultencoding()), end='',
                      file=sys.stderr)
                sys.stderr.flush()

    def _at_exit(self):
        if self.process is not None:
            self.process.terminate()

    def _timeout(self):
        report("Timeout run out. Terminating pkgupdate.")
        self.process.terminate()
        setup_alarm(self._kill_timeout, self.kill_timeout)
        self.process.wait()
        signal.alarm(0)

    def _kill_timeout(self):
        report("Kill timeout run out. Killing pkgupdate.")
        self.process.kill()


def run(ensure_run, timeout, timeout_kill, verbose):
    """Run updater
    """
    pidlock = PidLock()
    if not pidlock.acquire(ensure_run):
        sys.exit(1)
    exit_code = 0

    while True:
        pidlock.unblock()
        # TODO prepare approvals
        supervisor = Supervisor(verbose=verbose)
        report("Running pkgupdate")
        supervisor.run()
        exit_code = supervisor.join(timeout, timeout_kill)
        if exit_code != 0:
            report("pkgupdate exited with: " + str(exit_code))
        del supervisor  # To clean signals and more
        # TODO generate report
        # TODO evaluate approvals
        pidlock.block()
        if pidlock.sigusr1:
            report("Rerunning pkgupdate as requested.")
        else:
            break

    # TODO send notifications if needed
    # Note: pid_lock is freed using atexit
    return exit_code

