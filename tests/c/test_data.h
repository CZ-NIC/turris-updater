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
#include <util.h>

// Returns path to temporally directory used to run tests
const char *get_tmpdir();

// Returns path to directory with test data
const char *get_datadir();

// Creates template for mktemp and mkdtemp style of functions for path in tmpdir.
char *tmpdir_template(const char *identifier);


#define TEST_STRING "Simple test string for various string operation tests."

#define HTTP_APPLICATION_TEST "http://applications-test.turris.cz"
#define HTTPS_APPLICATION_TEST "https://applications-test.turris.cz"

// Lorem Ipsum
#define LOREM_IPSUM_SHORT "lorem ipsum\n"
#define LOREM_IPSUM_SHORT_SIZE 12
#define HTTP_LOREM_IPSUM_SHORT ( HTTP_APPLICATION_TEST "/li.txt" )
#define HTTP_LOREM_IPSUM ( HTTP_APPLICATION_TEST "/lorem_ipsum.txt" )
#define HTTPS_LOREM_IPSUM_SHORT ( HTTPS_APPLICATION_TEST "/li.txt" )
#define HTTPS_LOREM_IPSUM ( HTTPS_APPLICATION_TEST "/lorem_ipsum.txt" )
#define FILE_LOREM_IPSUM_SHORT aprintf("%s/lorem_ipsum_short.txt", get_datadir())
#define FILE_LOREM_IPSUM_SHORT_GZ aprintf("%s.gz", FILE_LOREM_IPSUM_SHORT)
#define FILE_LOREM_IPSUM_SHORT_XZ aprintf("%s.xz", FILE_LOREM_IPSUM_SHORT)
#define FILE_LOREM_IPSUM aprintf("%s/lorem_ipsum.txt", get_datadir())
#define FILE_LOREM_IPSUM_GZ aprintf("%s.gz", FILE_LOREM_IPSUM)

// Signatures
#define USIGN_KEY_1_PUB (aprintf("%s/usign.key1.pub", get_datadir()))
#define USIGN_KEY_2_PUB (aprintf("%s/usign.key2.pub", get_datadir()))
#define SIG_1_LOREM_IPSUM (aprintf("%s/lorem_ipsum.txt.sig", get_datadir()))
#define SIG_2_LOREM_IPSUM (aprintf("%s/lorem_ipsum.txt.sig2", get_datadir()))
#define SIG_1_LOREM_IPSUM_SHORT (aprintf("%s/lorem_ipsum_short.txt.sig", get_datadir()))
#define SIG_2_LOREM_IPSUM_SHORT (aprintf("%s/lorem_ipsum_short.txt.sig2", get_datadir()))

// Certificates
#define FILE_LETS_ENCRYPT_ROOTS aprintf("%s/lets_encrypt_roots.pem", get_datadir())
#define URI_FILE_LETS_ENCRYPT_ROOTS aprintf("file://%s/lets_encrypt_roots.pem", get_datadir())
#define FILE_OPENTRUST_CA_G1 aprintf("%s/opentrust_ca_g1.pem", get_datadir())
#define URI_FILE_OPENTRUST_CA_G1 aprintf("file://%s/opentrust_ca_g1.pem", get_datadir())

// Unpack_package
#define UNPACK_PACKAGE_VALID_IPK aprintf("%s/unpack_package/valid.ipk", get_datadir())

// Untar package to temporally directory using tar
char *untar_package(const char *ipk_path);

#endif
