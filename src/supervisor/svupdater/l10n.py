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
from uci import Uci, UciExceptionNotFound
from .const import L10N_FILE
from .exceptions import ExceptionUpdaterNoSuchLang


def languages():
    """Returns dict with all available l10n translations for system packages.
    """
    result = dict()

    if os.path.isfile(L10N_FILE):  # Just to be sure
        with open(L10N_FILE, 'r') as file:
            for line in file.readlines():
                if not line.strip():
                    continue  # ignore empty lines
                result[line.strip()] = False

    with Uci() as uci:
        try:
            l10n_enabled = uci.get("updater", "l10n", "langs")
        except (UciExceptionNotFound, KeyError):
            # If we fail to get that section then just ignore
            return result
    for lang in l10n_enabled:
        result[lang] = True

    return result


def update_languages(langs):
    """Updates what languages should be installed to system.
    langs is expected to be a list of strings where values are ISO languages
    codes.
    Note that this doesn't verifies that those languages are specified as
    supported in appropriate file.
    """
    # Verify langs
    expected = set()
    if os.path.isfile(L10N_FILE):  # Just to be sure
        with open(L10N_FILE, 'r') as file:
            for line in file.readlines():
                expected.add(line.strip())
    for lang in langs:
        if lang not in expected:
            raise ExceptionUpdaterNoSuchLang(
                "Can't enable unsupported language code:" + str(lang))

    # Set
    with Uci() as uci:
        uci.set('updater', 'l10n', 'l10n')
        uci.set('updater', 'l10n', 'langs', tuple(langs))
