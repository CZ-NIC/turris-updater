#!/usr/bin/python2.7

# Copyright (c) 2013, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL CZ.NIC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This is part of the updater utility. Do not run separately.

import sys
import os
import os.path
import logging
import logging.handlers

# Log both to stderr and to syslog.
logger = logging.getLogger('updater')
logger.addHandler(logging.StreamHandler(sys.stderr))
syslog = logging.handlers.SysLogHandler(address='/dev/log')
syslog.setFormatter(logging.Formatter('%(name)s %(message)s'))
logger.addHandler(syslog)
logger.setLevel(logging.DEBUG)
logger.debug('Consolidating')

PACKAGES_FILE = '/usr/share/updater/installed-packages'

def die(message):
    logger.error(message)
    sys.exit(1)

if len(sys.argv) < 4:
	die("Not enough parameters. Needs revision, device ID and at least one package list")

revision = sys.argv[1]
device = sys.argv[2]
lists = sys.argv[3:]

def store_packages(installed):
    # Write to a temporary file and rename - it is the safer way, with the filesystem we have to live on...
    with open(PACKAGES_FILE + '.tmp', 'w') as output:
	output.writelines(map(lambda p: p + '\n', installed))
    os.rename(PACKAGES_FILE + '.tmp', PACKAGES_FILE)

def load_packages():
    with open(PACKAGES_FILE) as packages:
	    return set(map(lambda l: l.strip(), packages))

def construct_packages(lists):
    # Get packages installed from all these lists, combine them together
    # (but without the ones that are scheduled for removal)
    installed = set()
    for plist in lists:
	    with open(plist) as packages:
		for package in packages:
			parts = package.split()
			(name, flags) = (parts[0], parts[2])
			if flags.find('R') == -1:
				installed.add(name)
    return installed

if not os.path.exists(PACKAGES_FILE):
    logger.info('No list of previously installed packages found, setting one up')
    installed = construct_packages(lists)

    store_packages(installed)
else:
    current = construct_packages(lists)
    previous = load_packages()
    # Find extra installed packages - the ones not required any more
    for extra in previous - current:
	    print(extra)
