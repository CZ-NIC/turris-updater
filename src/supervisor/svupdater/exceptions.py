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
"""updater-supervisor specific exceptions.
"""


class ExceptionUpdaterDisabled(Exception):
    """This exception is thrown when you try to run updater when it's
    configured to be disabled.
    """
    pass


class ExceptionUpdaterApproveInvalid(Exception):
    """Exception thrown from either approve.approve() or approve.deny() when
    given hash doesn't match the one from approve.current().
    """
    pass


class ExceptionUpdaterPidLockFailure(Exception):
    """This exception is thrown when we encounter some invalid usage of
    pidlock.
    """
    pass


class ExceptionUpdaterNoSuchList(Exception):
    """Exception thrown from lists.update when non-existent list is given.
    """
    pass


class ExceptionUpdaterNoSuchLang(Exception):
    """Exception thrown from l10n.update when unsupported language code is
    given.
    """
    pass


class ExceptionUpdaterInvalidHookCommand(Exception):
    """Thrown from hook.register when argument command contains more than one
    line.
    """
    pass
