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
from uci import Uci, UciExceptionNotFound

def enabled():
    """Returns True if updater can be automatically started by various system
    utils. This includes automatic periodic execution, after-boot recovery and
    other tools call to configuration aplication.
    Relevant uci configuration is: updater.autorun.enable
    """
    with Uci() as uci:
        try:
            # TODO use EUci instead of this retype (as this is not perfect)
            return not bool(int(uci.get("updater", "override", "disable")))
        except UciExceptionNotFound:
            return False  # No option means disabled


def set_enabled(enabled):
    """Set value that can be later received with enabled function.
    It sets uci configuration value: updater.autorun.enable
    """
    with Uci() as uci:
        uci.set('updater', 'override', 'override')
        uci.set('updater', 'override', 'disable', int(not bool(enabled)))


def approvals():
    """Returns True if updater approvals are enabled.
    Relevant uci configuration is: updater.autorun.approvals
    """
    with Uci() as uci:
        try:
            # TODO use EUci instead of this retype (as this is not perfect)
            return bool(int(uci.get("updater", "approvals", "need")))
        except UciExceptionNotFound:
            return False  # No option means disabled


def set_approvals(enabled):
    """Set value that can later be received by enabled function.
    This is relevant to uci config: updater.autorun.approvals
    """
    with Uci() as uci:
        uci.set('updater', 'approvals', 'approvals')
        uci.set('updater', 'approvals', 'need', int(bool(enabled)))


def auto_approve_time():
    """Returns number of hours before automatic approval is granted. If no
    approval time is configured then this function returns None.
    This is releavant to uci config: updater.autorun.auto_approve_time
    """
    with Uci() as uci:
        try:
            value = int(uci.get("updater", "approvals", "auto_grant_seconds"))
            return (value / 3600) if value > 0 else None
        except UciExceptionNotFound:
            return None


def set_auto_approve_time(approve_time):
    """Sets time in hours after which approval is granted. You can provide None
    or value that is less or equal to zero and in that case this feature is
    disabled and if approvals are enabled only manual approve can be granted.
    """
    with Uci() as uci:
        if approve_time > 0:
            uci.set('updater', 'approvals', 'approvals')
            uci.set('updater', 'approvals', 'auto_grant_seconds', int(approve_time) * 3600)
        else:
            uci.delete('updater', 'autorun', 'auto_approve_time')
