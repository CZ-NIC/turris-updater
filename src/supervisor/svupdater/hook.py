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
import os
import sys
import fcntl
import errno
import subprocess
from threading import Thread
from .utils import report
from ._pidlock import pid_locked
from .const import POSTRUN_HOOK_FILE
from .exceptions import ExceptionUpdaterInvalidHookCommand


def __run_command(command):
    def _fthread(file):
        while True:
            line = file.readline()
            if not line:
                break
            report(line.decode(sys.getdefaultencoding()))

    report('Running command: ' + command)
    process = subprocess.Popen(command, stderr=subprocess.PIPE,
                               stdout=subprocess.PIPE,
                               shell=True)
    tout = Thread(target=_fthread, args=(process.stdout,))
    terr = Thread(target=_fthread, args=(process.stderr,))
    tout.daemon = True
    terr.daemon = True
    tout.start()
    terr.start()
    exit_code = process.wait()
    if exit_code != 0:
        report('Command failed with exit code: ' + str(exit_code))


def register(command):
    """Add given command (format is expected to be same as if you call
    subprocess.run) to be executed when updater exits. Note that this hook is
    executed no matter if updater passed or failed or even if it just requested
    user's approval. In all of those cases when updater exits this hook is
    executed.

    "commands" has to be single line shell script.
    """
    if '\n' in command:
        raise ExceptionUpdaterInvalidHookCommand(
            "Argument register can be only single line string.")
    # Open file for writing and take exclusive lock
    file = os.open(POSTRUN_HOOK_FILE, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
    fcntl.lockf(file, fcntl.LOCK_EX)
    # Check if we are working with existing file
    invalid = False
    try:
        if os.fstat(file).st_ino != os.stat(POSTRUN_HOOK_FILE).st_ino:
            invalid = True
    except OSError as excp:
        if excp.errno == errno.ENOENT:
            invalid = True
        raise
    if invalid:  # File was removed before we locked it
        os.close(file)
        register(command)
        return
    if not pid_locked():  # Check if updater is running
        os.close(file)
        # If there is no running instance then just run given command
        __run_command(command)
        return
    # Append given arguments to file
    # Note: This takes ownership of file and automatically closes it. (at least
    # it seems that way)
    with os.fdopen(file, 'w') as fhook:
        fhook.write(command + '\n')
    report('Postrun hook registered: ' + command)


def register_list(commands):
    """Same as register but it allows multiple commands to be registered at
    once.
    """
    if commands is not None:
        for cmd in commands:
            register(cmd)


def _run():
    """Run all registered commands.
    """
    # Open file for reading and take exclusive lock
    try:
        file = os.open(POSTRUN_HOOK_FILE, os.O_RDWR)
    except OSError as excp:
        if excp.errno == errno.ENOENT:
            return  # No file means nothing to do
        raise
    fcntl.lockf(file, fcntl.LOCK_EX)
    # Note: nobody except us should be able to remove this file (because we
    # should hold pidlock) so we don't have to check if file we opened is still
    # on FS.
    with os.fdopen(file, 'r') as fhook:
        for line in fhook.readlines():
            __run_command(line)
        os.remove(POSTRUN_HOOK_FILE)
