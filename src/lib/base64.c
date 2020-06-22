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
#include "base64.h"
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include "logging.h"

static bool base64_is_valid_char(const char c) {
	return \
		(c >= '0' && c <= '9') || \
		(c >= 'A' && c <= 'Z') || \
		(c >= 'a' && c <= 'z') || \
		(c == '+' || c == '/' || c == '=');
}

unsigned base64_valid(const char *data, size_t len) {
	// TODO this is only minimal verification, we should do more some times in future
	for (size_t i = 0; i < len; i++)
		if (!base64_is_valid_char(data[i]))
			return i;
	return len;
}

size_t base64_decode_len(const char *data, size_t len) {
	size_t padding = 0;

	if (data != NULL)
		padding = data[len - 1] == '=' ? (data[len - 2] == '=' ? 2 : 1) : 0;

	return (len * 3 / 4) - padding;
}

size_t base64_decode_allocate(const char *data, size_t len, uint8_t **buff) {
	size_t decode_len = base64_decode_len(data, len);
	// Note: calloc here is intentional. OpenSSL behaves interestingly and
	// valgrind because of that reports that some bytes are not initialized. This
	// way we zero them all thanks to that make it all initialized.
	*buff = calloc(decode_len + 1, 1);
	return decode_len;
}

bool base64_decode(const char *data, size_t data_len, uint8_t *buff) {
	BIO *bio= BIO_new_mem_buf(data, data_len);
	BIO *b64 = BIO_new(BIO_f_base64());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
	int len = BIO_read(bio, buff, base64_decode_len(data, data_len));
	BIO_free_all(bio);

	if (len <= 0 && would_log(LL_TRACE))
		TRACE("base64 decode failed (%.*s): %s", (int)data_len, data,
				ERR_error_string(ERR_get_error(), NULL));
	return len > 0;
}
