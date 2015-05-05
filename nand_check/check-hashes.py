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
files = json.loads(open('/tmp/hashes.json').read())
broken = set()

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
		except IOError:
			logger.warning("Couldn't read file %s of %s", f, name)
