/*
 * Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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

/*
 * This module provides generic main for the C based tests.
 */

#ifndef C_TEST_H
#define C_TEST_H

#include <check.h>

/*
 * Header of the function a test case should provide. It shall
 * create a test suite, fill it with tests and pass as a result.
 * It will get called by the provided main() function.
 */
Suite *gen_test_suite(void) __attribute__((returns_nonnull));

#endif
