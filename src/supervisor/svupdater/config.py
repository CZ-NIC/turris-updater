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


class Config:
    "Updater's configuration wrapper."
    def __init__(self):
        self.uci = None

    def __enter__(self):
        self.uci = Uci()
        return self

    def __exit__(self, *args):
        del self.uci
        self.uci = None

    def _get(self, package, section, option, req_type):
        "Internal uci_get function"
        if self.uci is None:
            return None
        try:
            value = self.uci.get(package, section, option)
        except UciExceptionNotFound:
            return None
        # TODO use EUci instead of this retype (as this is not perfect)
        if req_type == bool:
            return bool(int(value))
        return req_type(value)

    def disable(self):
        """Returns True if updater is set to be disabled.
        This is config: updater.override.disable
        """
        return self._get("updater", "override", "disable", bool)

    def branch(self):
        """Return name of configured branch. But on top of that if nothing is
        configured then it returns deploy instead of empty string.
        This is config: updater.override.branch
        """
        branch = self._get("updater", "override", "branch", str)
        if not branch:
            branch = "deploy"
        return branch
