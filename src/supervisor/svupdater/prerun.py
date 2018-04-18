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
"""These are functions we use before we even take pid lock file. They allow
updater-supervisor to be suspended for random amount of time or it allows it to
wait for internet connection
"""
import os
import subprocess
import time
from random import randrange
from multiprocessing import Process
from .const import PING_ADDRESS
from .utils import report


def random_sleep(max_seconds):
    "Sleep random amount of seconds with maximum of max_seconds"
    if max_seconds is None or max_seconds <= 0:
        return  # No sleep at all
    suspend = randrange(max_seconds)
    if suspend > 0:  # Just nice to have no print if we wait for 0 seconds
        report("Suspending updater start for " + str(suspend) + " seconds")
    time.sleep(suspend)


def wait_for_network(max_stall):
    """This tries to connect to repo.turris.cz to check if we can access it and
    otherwise it stalls execution for given maximum number of seconds.
    """
    def ping():
        """Just run one second timeout single ping to check if we have
        connection """
        with open(os.devnull, 'w') as devnull:
            return subprocess.call(
                ['ping', '-c', '1', '-w', '1', PING_ADDRESS],
                stdin=devnull,
                stdout=devnull,
                stderr=devnull
                )

    def network_test():
        "Run network test (expected to be run as subprocess)"
        if ping():
            report("Waiting for network connection")
            while ping():
                pass

    if max_stall is None:
        return  # None means no stall
    process = Process(target=network_test)
    process.start()
    process.join(max_stall)
    if process.is_alive():
        process.terminate()
