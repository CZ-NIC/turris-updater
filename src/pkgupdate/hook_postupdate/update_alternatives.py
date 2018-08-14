#!/usr/bin/env python3
import os

_INFO_D_ = "/usr/lib/opkg/info"


altfs = dict()

for fname in os.listdir(_INFO_D_):
    path = os.path.join(_INFO_D_, fname)
    if not os.path.isfile(path) or not fname.endswith('.control'):
        continue
    with open(path) as file:
        for line in file:
            if line.startswith('Description:'):
                break
            if not line.startswith('Alternatives:'):
                continue
            for alt in line[13:].split(','):
                col = alt.strip().split(':')
                src = col[1]
                priority = int(col[0])
                if src not in altfs or altfs[src]["priority"] < priority:
                    altfs[src] = {"priority": priority, "target": col[2]}
            break

for alt, res in altfs.items():
    if os.path.exists(alt):
        if os.path.islink(alt):
            if os.readlink(alt) == res['target']:
                continue
            print("Changing link: {} -> {} ({})".format(
                alt, res['target'], os.readlink(alt)))
        os.remove(alt)
    else:
        print("Creating new link: {} -> {}".format(alt, res['target']))
    os.symlink(res['target'], alt)
