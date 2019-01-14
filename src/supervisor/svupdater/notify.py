# coding=utf-8

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
import subprocess
from .utils import report
from .const import PKGUPDATE_LOG, NOTIFY_MESSAGE_CS, NOTIFY_MESSAGE_EN
from .const import PKGUPDATE_ERROR_LOG, PKGUPDATE_CRASH_LOG
if sys.version_info < (3, 0):
    import approvals
else:
    from . import approvals


def clear_logs():
    """Remove files updater dumps when it detects failure.
    """
    if os.path.isfile(PKGUPDATE_ERROR_LOG):
        os.remove(PKGUPDATE_ERROR_LOG)
    if os.path.isfile(PKGUPDATE_CRASH_LOG):
        os.remove(PKGUPDATE_CRASH_LOG)


def failure(exit_code, trace):
    """Send notification about updater's failure
    """
    if exit_code == 0 and not os.path.isfile(PKGUPDATE_ERROR_LOG):
        return

    msg_en = "Updater selhal: "
    msg_cs = "Updater failed: "

    if os.path.isfile(PKGUPDATE_ERROR_LOG):
        with open(PKGUPDATE_ERROR_LOG, 'r') as file:
            content = '\n'.join(file.readlines())
        msg_en += content
        msg_cs += content
    elif os.path.isfile(PKGUPDATE_CRASH_LOG):
        with open(PKGUPDATE_CRASH_LOG, 'r') as file:
            content = '\n'.join(file.readlines())
        msg_en += content
        msg_cs += content
    elif trace is not None:
        msg_en += trace + "\n\nExit code: " + str(exit_code)
        msg_cs += trace + "\n\nNávratový kód: " + str(exit_code)
    else:
        msg_en += "Unknown error"
        msg_cs += "Neznámá chyba"

    if subprocess.call(['create_notification', '-s', 'error',
                        msg_cs, msg_en]) != 0:
        report('Notification creation failed.')

    clear_logs()


def changes():
    """Send notification about changes.
    """
    if not os.path.isfile(PKGUPDATE_LOG):
        return

    text_en = ""
    text_cs = ""
    with open(PKGUPDATE_LOG, 'r') as file:
        for line in file.readlines():
            pkg = line.split(' ')
            if pkg[0].strip() == 'I':
                text_en += " • Installed version {} of package {}\n".format(
                    pkg[2].strip(), pkg[1].strip())
                text_cs += " • Nainstalovaná verze {} balíku {}\n".format(
                    pkg[2].strip(), pkg[1].strip())
            elif pkg[0].strip() == 'R':
                text_en += " • Removed package {}\n".format(pkg[1].strip())
                text_cs += " • Odstraněn balík {}\n".format(pkg[1].strip())
            elif pkg[0].strip() == 'D':
                # Ignore package downloads
                pass
            else:
                report("Unknown log entry: " + line.strip())

    if text_en and text_cs:
        if subprocess.call(['create_notification', '-s', 'update',
                            text_cs.encode(sys.getdefaultencoding()),
                            text_en.encode(sys.getdefaultencoding())
                            ]) != 0:
            report('Notification creation failed.')

    os.remove(PKGUPDATE_LOG)


def approval():
    """Send notification about approval request.
    """
    apprv = approvals.current()
    text = ""
    for pkg in apprv['plan']:
        text += u"\n • {0} {1} {2}".format(
            pkg['op'].title(), pkg['name'],
            "" if pkg['new_ver'] is None else pkg['new_ver'])
    if subprocess.call(['create_notification', '-s', 'update',
                        NOTIFY_MESSAGE_CS + text, NOTIFY_MESSAGE_EN + text]) \
            != 0:
        report('Notification creation failed.')


def notifier():
    """This just calls notifier. It processes new notification and sends them
    together.
    """
    if subprocess.call(['notifier']) != 0:
        report('Notifier failed')
