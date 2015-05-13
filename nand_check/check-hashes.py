#!/usr/bin/python

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

for pkg in packages:
	ver = versions[pkg]
	name = pkg + '-' + ver
	try:
		hashes = files[name]
	except KeyError:
		logger.warning('No info about package %s, assuming being empty', name)
		continue
	for f in hashes:
		try:
			with open(f) as i:
				m = hashlib.md5()
				m.update(i.read())
				h = m.hexdigest()
			if h != hashes[f]:
				logger.warning("Hash for file %s of %s does not match, got %s, expected %s", f, name, h, hashes[f])
				broken_files.add(f)
				broken[pkg] = ver
		except IOError:
			logger.warning("Couldn't read file %s of %s", f, name)
			broken[pkg] = ver
		except UnicodeEncodeError:
			logger.warning("Broken unicode in file name %s of %s", f, name)

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
