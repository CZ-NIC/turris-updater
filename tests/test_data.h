/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the turris updater.
 *
 * Updater is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 * Updater is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Updater.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef UPDATER_TEST_DATA_H
#define UPDATER_TEST_DATA_H
#include "../src/lib/util.h"

const char *get_tmpdir();
const char *get_sdir();

#define HTTP_APPLICATION_TEST "http://applications-test.turris.cz"
#define HTTPS_APPLICATION_TEST "https://applications-test.turris.cz"

// Lorem Ipsum
#define LOREM_IPSUM_SHORT "lorem ipsum\n"
#define LOREM_IPSUM_SHORT_SIZE 12
#define HTTP_LOREM_IPSUM_SHORT ( HTTP_APPLICATION_TEST "/li.txt" )
#define HTTP_LOREM_IPSUM ( HTTP_APPLICATION_TEST "/lorem_ipsum.txt" )
#define HTTPS_LOREM_IPSUM_SHORT ( HTTPS_APPLICATION_TEST "/li.txt" )
#define HTTPS_LOREM_IPSUM ( HTTPS_APPLICATION_TEST "/lorem_ipsum.txt" )
#define FILE_LOREM_IPSUM_SHORT aprintf("%s/tests/data/lorem_ipsum_short.txt", get_sdir())
#define FILE_LOREM_IPSUM_SHORT_GZ aprintf("%s.gz", FILE_LOREM_IPSUM_SHORT)
#define FILE_LOREM_IPSUM_SHORT_XZ aprintf("%s.xz", FILE_LOREM_IPSUM_SHORT)
#define FILE_LOREM_IPSUM aprintf("%s/tests/data/lorem_ipsum.txt", get_sdir())
#define FILE_LOREM_IPSUM_GZ aprintf("%s.gz", FILE_LOREM_IPSUM)

// Signatures
#define USIGN_KEY_1_PUB (aprintf("%s/tests/data/usign.key1.pub", get_sdir()))
#define USIGN_KEY_2_PUB (aprintf("%s/tests/data/usign.key2.pub", get_sdir()))
#define SIG_1_LOREM_IPSUM (aprintf("%s/tests/data/lorem_ipsum.txt.sig", get_sdir()))
#define SIG_2_LOREM_IPSUM (aprintf("%s/tests/data/lorem_ipsum.txt.sig2", get_sdir()))
#define SIG_1_LOREM_IPSUM_SHORT (aprintf("%s/tests/data/lorem_ipsum_short.txt.sig", get_sdir()))
#define SIG_2_LOREM_IPSUM_SHORT (aprintf("%s/tests/data/lorem_ipsum_short.txt.sig2", get_sdir()))

// Certificates
#define FILE_LETS_ENCRYPT_ROOTS aprintf("%s/tests/data/lets_encrypt_roots.pem", get_sdir())
#define URI_FILE_LETS_ENCRYPT_ROOTS aprintf("file://%s/tests/data/lets_encrypt_roots.pem", get_sdir())
#define FILE_OPENTRUST_CA_G1 aprintf("%s/tests/data/opentrust_ca_g1.pem", get_sdir())
#define URI_FILE_OPENTRUST_CA_G1 aprintf("file://%s/tests/data/opentrust_ca_g1.pem", get_sdir())

#endif
