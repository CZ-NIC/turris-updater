/* Copyright (c) 2020 CZ.NIC z.s.p.o. (http://www.nic.cz/)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

static Suite **suites = NULL;
static size_t suites_len = 0, suites_size = 1;

void unittests_add_suite(Suite *s) {
	if (suites == NULL || suites_len == suites_size)
		suites = realloc(suites, (suites_size *= 2) * sizeof *suites);
	suites[suites_len++] = s;
}


int main(void) {
	SRunner *runner = srunner_create(NULL);

	for (size_t i = 0; i < suites_len; i++)
		srunner_add_suite(runner, suites[i]);

	char *test_output_tap = getenv("TEST_OUTPUT_TAP");
	if (test_output_tap && *test_output_tap != '\0')
		srunner_set_tap(runner, test_output_tap);
	char *test_output_xml = getenv("TEST_OUTPUT_XML");
	if (test_output_xml && *test_output_xml != '\0')
		srunner_set_xml(runner, test_output_xml);
	srunner_set_fork_status(runner, CK_FORK); // We have to fork to catch signals

	srunner_run_all(runner, CK_NORMAL);
	int failed = srunner_ntests_failed(runner);

	srunner_free(runner);
	return (bool)failed;
}
