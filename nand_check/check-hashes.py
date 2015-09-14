#!/usr/bin/python

# Copyright (c) 2015 CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

import sys
import subprocess
import json
import hashlib
import logging
import logging.handlers

logger = logging.getLogger('updater-hash-check')
logger.addHandler(logging.StreamHandler(sys.stderr))
syslog = logging.handlers.SysLogHandler(address='/dev/log')
syslog.setFormatter(logging.Formatter('%(name)s %(message)s'))
logger.addHandler(syslog)
logger.setLevel(logging.DEBUG)

logger.debug('Checking hashes')

versions = dict(map(lambda line: line.split(' - '), subprocess.check_output(['opkg', 'list-installed']).splitlines()))
packages = open('/usr/share/updater/installed-packages').read().splitlines()
files = json.loads(open('/tmp/update/hashes.json').read())
def pkg_info_extract(line):
	words = line.split('\t')
	return words[0], words[1:]
pkg_info = dict(map(pkg_info_extract, open('/tmp/update/all_lists').read().splitlines()))
broken = {}
broken_files = set()

def get_hashes(name, pkg, version):
	fname = '/usr/share/updater/hashes/' + pkg + '---' + version + '.json'
	try:
		return [json.loads(open(fname).read())], True, fname
	except IOError:
		try:
			result = files[name]
		except KeyError:
			logger.warning('No info about package %s, assuming being empty', name)
			return {}, False, fname
		logger.info('Hash for %s not stored, using the server version', name)
		return result, False, fname

for pkg in packages:
	ver = versions[pkg]
	name = pkg + '-' + ver
	hash_options, saved, fname = get_hashes(name, pkg, ver)
	broken_candidate = None
	for hashes in hash_options:
		bad_files = {}
		for f in hashes:
			try:
				with open(f) as i:
					m = hashlib.md5()
					m.update(i.read())
					h = m.hexdigest()
				if h != hashes[f]:
					bad_files[f] = {'reason': 'Hash', 'got': h, 'expected': hashes[f]}
			except IOError:
				bad_files[f] = {'reason': 'Missing'}
			except UnicodeEncodeError:
				logger.warning("Broken unicode in file name %s of %s", f, name)
		if not bad_files:
			if not saved:
				with open(fname, 'w') as f:
					f.write(json.dumps(hashes))
			break # We found a candidate for good hashes, next package please
		else:
			broken_candidate = bad_files
	if broken_candidate:
		for f in broken_candidate:
			info = broken_candidate[f]
			if info['reason'] == 'Hash':
				logger.warning("Hash for file %s of %s does not match, got %s, expected %s", f, name, info['got'], info['expected'])
				broken_files.add(f)
				broken[pkg] = ver
			else:
				logger.warning("Couldn't read file %s of %s", f, name)
				broken[pkg] = ver

if not broken:
	sys.exit()

with open('/tmp/update/hash.reinstall', 'w') as o:
	o.write("mkdir -p /tmp/broken-files\n")
	for f in broken_files:
		o.write("cp '" + f + "' /tmp/broken-files\n")
	for pkg in broken:
		info = pkg_info[pkg]
		assert(info[0] == broken[pkg]) # The version matches
		flags = info[1]
		for bidden in ['U', 'I', 'B']:
			flags = flags.replace(bidden, '')
		o.write("get_package '" + pkg + "' '" + broken[pkg] + "' '" + flags + "' '" + info[2] + "'\n");
		o.write('mv "$TMP_DIR/package.ipk" "$PKG_DIR"/\'' + pkg + "'.ipk\n")
		o.write("do_install '" + pkg + "' '" + broken[pkg] + "' '" + flags + "'\n")
