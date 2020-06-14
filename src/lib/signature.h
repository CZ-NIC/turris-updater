/*
 * Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * This file is part of the Turris Updater.
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
#ifndef UPDATER_SIGNATURE_H
#define UPDATER_SIGNATURE_H
#include <stdbool.h>
#include "util.h"

enum sign_errors {
	SIGN_NO_ERROR = 0,
	SIGN_ERR_KEY_FORMAT, // Loaded key has invalid format (size does not match)
	SIGN_ERR_SIG_FORMAT, // KEY_FORMAT error variant for signatures
	SIGN_ERR_KEY_UNKNOWN, // Key loaded but it has unknown format or type
	SIGN_ERR_SIG_UNKNOWN, // KEY_UNKNOWN error variant for signatures
	SIGN_ERR_NO_MATHING_KEY, // Non of provided keys was used to sign provided message
	SIGN_ERR_VERIFY_FAIL, // Provided message was corrupted (signature does not match)
};

extern THREAD_LOCAL enum sign_errors sign_errno;

struct sign_pubkey;

// Create new pubkey object
// key: key data. They are copied so it is not required to keep them around after
//   this function terminates.
// len: key data length.
// Returns sign_pubkey object or NULL on error.
// Possible errors: SIGN_ERR_KEY_FORMAT, SIGN_ERR_KEY_UNKNOWN
struct sign_pubkey *sign_pubkey(const uint8_t *key, size_t len) __attribute__((malloc));

// Free pubkey object
void sign_pubkey_free(struct sign_pubkey*);

// Verify provided message against provided signature and list of pubkeys.
// data: pointer to message to verify
// data_len: size of message in bytes
// sign: pointer to signature of message
// sign_len: size of signature in bytes
// sign_pubkey: NULL terminated array of pubkey objects
// Returns true if message is verified and false if not or error was encountered.
// Possible errors: SIGN_ERR_NO_MATHING_KEY, SIGN_ERR_VERIFY_FAIL
bool sign_verify(const void *data, size_t data_len,
		const void *sign, size_t sign_len,
		const struct sign_pubkey* const*);

// Provides string describing signature error
// number: signature error number
// Returns string with message describing error. You should not modify this
// message.
const char *sign_strerror(enum sign_errors number);

#endif
