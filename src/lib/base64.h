/*
 * Copyright 2018-2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#ifndef UPDATER_BASE64_H
#define UPDATER_BASE64_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
// This is OpenSSL based base64 decoding utility

// Verify if given data are encoded in base64 format
// data: pointer to start of data
// len: size of data
// It returns len if data are valid base64 format, it returns index of problematic
// character otherwise.
unsigned base64_valid(const char *data, size_t len);

// Analyze provided data and return appropriate buffer size
// data: pointer to data start (can be NULL and in that case maximal required
//   buffer for len length of string is returned)
// len: size of data (use strlen for string or just specify exact size)
// Returns exact output size for provided string or maximum size for NULL
size_t base64_decode_len(const char *data, size_t len);

// base64_decode_len variant that allocates appropriately sized buffer with one
// additional byte set to zero at the end.
// User should free returned memory after use.
size_t base64_decode_allocate(const char *data, size_t len, uint8_t **buff);

// Decode base64 encoded data in data to buff
// data: pointer to start of base64 encoded data
// data_len: size of data string
// buff: buffer of at least base64_decode_len provided length to write data to.
// Returns true if decoding was successful and false otherwise.
bool base64_decode(const char *data, size_t data_len, uint8_t *buff);


#endif
