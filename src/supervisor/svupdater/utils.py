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
"""Various utility functions used in more than one other updater-supervisor
module.
"""
from __future__ import print_function
import os
import sys
import fcntl
import errno
import resource
import signal
import traceback
import syslog


def report(msg):
    """Report message to syslog and to terminal.
    """
    if sys.stderr.isatty():
        print("\x1b[32mSupervisor\x1b[0m:" + msg, file=sys.stderr)
    else:
        print("Supervisor:" + msg, file=sys.stderr)
    syslog.syslog(msg)


def setup_alarm(func, timeout):
    "This is simple alarm setup function with possibility of None timeout"
    if timeout is None:
        return
    signal.signal(signal.SIGALRM, func)
    signal.alarm(timeout)


def check_exclusive_lock(path, isflock=False):
    """This returns True if someone holds exclusive lock on given path.
    Otherwise it returns False.
    """
    try:
        file = os.open(path, os.O_RDWR)
    except (IOError, OSError) as excp:
        if excp.errno == errno.ENOENT:
            # There is no such file so no lock
            return False
        raise
    try:
        if isflock:
            fcntl.flock(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        else:
            fcntl.lockf(file, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError as excp:
        os.close(file)
        if excp.errno == errno.EACCES or excp.errno == errno.EAGAIN:
            # We can't take lock so someone holds it
            return True
        raise
    os.close(file)
    # We successfully locked file so no one holds its lock
    return False


def daemonize():
    """Fork to daemon. It returns True for parent process and False for child
    process.

    This does double fork to lost parent. And it closes standard pipes.
    """
    # First fork
    fpid = os.fork()
    if fpid != 0:
        os.waitpid(fpid, 0)
        return True
    # Set process name (just to distinguish it from parent process
    sys.argv[0] = 'updater-supervisor'
    # Second fork
    if os.fork() != 0:
        os._exit(0)
    # Setup syslog
    syslog.openlog('updater-supervisor')
    # Setup exceptions reporting hook
    sys.excepthook = lambda type, value, tb: report(
        ' '.join(traceback.format_exception(type, value, tb)))
    # Disconnect from ubus if connected
    try:
        import ubus
        if ubus.get_connected():
            ubus.disconnect(False)
    except Exception as excp:
        report("Ubus disconnect failed: " + str(excp))
    # Close all non-standard file descriptors
    for fd in range(3, resource.getrlimit(resource.RLIMIT_NOFILE)[0]):
        try:
            os.close(fd)
        except OSError:
            pass
    # Redirect standard outputs and input to devnull
    devnull = os.open(os.devnull, os.O_WRONLY)
    os.dup2(devnull, 0)
    os.dup2(devnull, 1)
    os.dup2(devnull, 2)
    os.close(devnull)
    return False
