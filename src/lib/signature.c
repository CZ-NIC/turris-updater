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
#include "signature.h"
#include <string.h>
#include <fcntl.h>
#include <b64/cdecode.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include "logging.h"

#define PUBLIC_KEY_SIZE 32
#define SIGNATURE_SIZE 64
#define FINGERPRINT_SIZE 8

THREAD_LOCAL enum sign_errors sign_errno;

static const char *error_strings[] = {
	[SIGN_NO_ERROR] = NULL,
	[SIGN_ERR_KEY_FORMAT] = "Public key has invalid format",
	[SIGN_ERR_SIG_FORMAT] = "Signature has invalid format",
	[SIGN_ERR_KEY_UNKNOWN] = "Public key is invalid or has unknown type",
	[SIGN_ERR_SIG_UNKNOWN] = "Signature is invalid or has unknown type",
	[SIGN_ERR_NO_MATHING_KEY] = "No public key with matching signature was provided",
	[SIGN_ERR_VERIFY_FAIL] = "Data or signature are corrupted",
};

struct sign_pubkey {
	char pkalg[2];
	uint8_t fingerprint[FINGERPRINT_SIZE];
	uint8_t pubkey[PUBLIC_KEY_SIZE];
};

struct sig {
	char pkalg[2];
	uint8_t fingerprint[FINGERPRINT_SIZE];
	uint8_t sig[SIGNATURE_SIZE];
};

static bool key_load_generic(const uint8_t *data, size_t len, void *key, size_t key_len) {
	// Skip first line as that contains only just comment
	size_t index = 0;
	while (index < len && data[index++] != '\n');

	char *buff = malloc(((len - index) * 3 / 4) + 1);
	base64_decodestate s;
	base64_init_decodestate(&s);
	size_t cnt = base64_decode_block((const void*)(data + index), len - index, buff, &s);

	if (cnt != key_len) {
		free(buff);
		TRACE("Key size mismatch: got %zd but key should be %zd", cnt, key_len);
		sign_errno = SIGN_ERR_KEY_FORMAT;
		return false;
	}

	memcpy(key, buff, key_len);
	free(buff);

	// Sanity check key (pkalg or in other words two initial bytes)
	if (strncmp("Ed", key, 2)) {
		TRACE("Key type mismatch: got '%.2s' but key should be 'Ed'", key);
		sign_errno = SIGN_ERR_KEY_UNKNOWN;
		return false;
	}

	return true;
}

struct sign_pubkey *sign_pubkey(const uint8_t *key, size_t len) {
	struct sign_pubkey *pubkey = malloc(sizeof *pubkey);
	if (key_load_generic(key, len, pubkey, sizeof *pubkey))
		return pubkey;
	free(pubkey);
	return NULL;
}

void sign_pubkey_free(struct sign_pubkey *key) {
	free(key);
}

// We use this function to report generic openssl error we do not expect to happend
static bool openssl_error() {
	DBG("OpenSSL error: %s", ERR_error_string(ERR_get_error(), NULL));
	// Just say that this is verify error even if that is not true possibly
	// It should simplify error handling. OpenSSL errors can happen only if input
	// is horibly wrong such as if someone tempered with keys.
	sign_errno = SIGN_ERR_VERIFY_FAIL;
	return false;
}

bool sign_verify(const void *data, size_t data_len, const void *sign,
		size_t sign_len, const struct sign_pubkey *const *pubkeys) {
	struct sig sig;
	if (!key_load_generic(sign, sign_len, &sig, sizeof sig)) {
		sign_errno = sign_errno == SIGN_ERR_KEY_FORMAT ? SIGN_ERR_SIG_FORMAT :
				sign_errno == SIGN_ERR_KEY_UNKNOWN ? SIGN_ERR_SIG_UNKNOWN :
				sign_errno;
		return false;
	}

	// Locate appropriate key by comparing fingerprint
	while (*pubkeys && memcmp(sig.fingerprint, (*pubkeys)->fingerprint, FINGERPRINT_SIZE))
		pubkeys++;
	if (!*pubkeys) {
		sign_errno = SIGN_ERR_NO_MATHING_KEY;
		return false;
	}
	EVP_PKEY *pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL,
			(*pubkeys)->pubkey, PUBLIC_KEY_SIZE);
	if (!pkey)
		return openssl_error();

	bool res = false;
	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	if (!EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, pkey)) {
		openssl_error();
		goto cleanup;
	}
	switch (EVP_DigestVerify(mdctx, sig.sig, SIGNATURE_SIZE, data, data_len)) {
		case 1:
			res = true;
			break;
		case 0:
			if (would_log(LL_TRACE))
				TRACE("Verify failed: %s", ERR_error_string(ERR_get_error(), NULL));
			sign_errno = SIGN_ERR_VERIFY_FAIL;
			break;
		default:
			openssl_error();
	}

cleanup:
	EVP_MD_CTX_free(mdctx);
	EVP_PKEY_free(pkey);
	return res;
}

const char *sign_strerror(enum sign_errors number) {
	return error_strings[number];
}
