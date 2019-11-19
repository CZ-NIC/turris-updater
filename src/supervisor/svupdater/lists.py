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
import json
import gettext
from euci import EUci, UciExceptionNotFound
from .const import USERLISTS_FILE
from .exceptions import ExceptionUpdaterNoSuchList


def userlists(lang=None):
    """Returns dict of userlists.
    Argument lang is expected to be a string containing language code. This
    code is then used for gettext translations of titles and descriptions of
    messages.

    Return userlists are in dictionary where key is name of userlist and value
    is another dictionary with following content:
    "enabled": This is boolean value containing info if userlist is enabled.
    "hidden": This is boolean value specifying if userlist is user visible.
    "title": This is title text describing userlist (human readable name). This
        field can be None if "hidden" field is set to True.
    "message": This is human readable description of given userlist. This can
        be None if "hidden" is set to True.
    """
    result = dict()

    trans = gettext.translation(
        'userlists',
        languages=[lang] if lang is not None else None,
        fallback=True)

    if os.path.isfile(USERLISTS_FILE):  # Just to be sure
        with open(USERLISTS_FILE, 'r') as file:
            ldul = json.load(file)
            for name, lst in ldul.items():
                visible = lst['visible']
                result[name] = {
                    "title": trans.gettext(lst['title']) if 'title' in lst else None,
                    "message": trans.gettext(lst['description']) if 'description' in lst else None,
                    "enabled": False,
                    "hidden": not visible
                }

    with EUci() as uci:
        lists = uci.get("updater", "pkglists", "lists", list=True, default=[])
    for lst in lists:
        if lst in result:
            result[lst]['enabled'] = True
        # Ignore any unknown but enabled lists

    return result


def update_userlists(lists):
    """
    List is expected to be a array of strings (list ids) that should be
    enabled. Anything omitted will be disabled.
    """
    expected = set()
    if os.path.isfile(USERLISTS_FILE):  # Just to be sure
        with open(USERLISTS_FILE, 'r') as file:
            ldul = json.load(file)
            for name in ldul:
                expected.add(name)
    for lst in lists:
        if lst not in expected:
            raise ExceptionUpdaterNoSuchList(
                "Can't enable unknown package list:" + str(lst))

    # Set
    with EUci() as uci:
        uci.set('updater', 'pkglists', 'pkglists')
        uci.set('updater', 'pkglists', 'lists', lists)


def pkglists(lang=None):
    """4.x backport"""
    return userlists(lang)


def update_pkglists(lists):
    """4.x backport"""
    update_userlists(lists)
