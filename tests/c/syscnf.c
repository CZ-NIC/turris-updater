/*
 * Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
#include "ctest.h"
#include <syscnf.h>
#include <util.h>
#include "test_data.h"

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#define SUFFIX_STATUS_FILE "usr/lib/opkg/status"
#define SUFFIX_INFO_DIR "usr/lib/opkg/info/"
#define SUFFIX_PKG_UNPACKED_DIR "usr/share/updater/unpacked/"
#define SUFFIX_PKG_DOWNLOAD_DIR "usr/share/updater/download/"
#define SUFFIX_DIR_OPKG_COLLIDED "usr/share/updater/collided/"

void paths_teardown() {
	set_root_dir(NULL);
}

START_TEST(default_paths) {
	set_root_dir(NULL);
	ck_assert_str_eq("/", root_dir());
	ck_assert_str_eq("/" SUFFIX_STATUS_FILE, status_file());
	ck_assert_str_eq("/" SUFFIX_INFO_DIR, info_dir());
	ck_assert_str_eq("/" SUFFIX_PKG_UNPACKED_DIR, pkg_unpacked_dir());
	ck_assert_str_eq("/" SUFFIX_PKG_DOWNLOAD_DIR, pkg_download_dir());
	ck_assert_str_eq("/" SUFFIX_DIR_OPKG_COLLIDED, opkg_collided_dir());
}
END_TEST

START_TEST(absolute_paths) {
#define ABS_ROOT "/tmp/updater-root/"
	set_root_dir(ABS_ROOT);
	ck_assert_str_eq(ABS_ROOT, root_dir());
	ck_assert_str_eq(ABS_ROOT SUFFIX_STATUS_FILE, status_file());
	ck_assert_str_eq(ABS_ROOT SUFFIX_INFO_DIR, info_dir());
	ck_assert_str_eq(ABS_ROOT SUFFIX_PKG_UNPACKED_DIR, pkg_unpacked_dir());
	ck_assert_str_eq(ABS_ROOT SUFFIX_PKG_DOWNLOAD_DIR, pkg_download_dir());
	ck_assert_str_eq(ABS_ROOT SUFFIX_DIR_OPKG_COLLIDED, opkg_collided_dir());
#undef ABS_ROOT
}
END_TEST

START_TEST(relative_paths) {
	char *cwd = getcwd(NULL, 0);
	set_root_dir("updater-root/");
#define PTH(SUFFIX) aprintf("%s/updater-root/%s", cwd, SUFFIX)
	ck_assert_str_eq(PTH(""), root_dir());
	ck_assert_str_eq(PTH(SUFFIX_STATUS_FILE), status_file());
	ck_assert_str_eq(PTH(SUFFIX_INFO_DIR), info_dir());
	ck_assert_str_eq(PTH(SUFFIX_PKG_UNPACKED_DIR), pkg_unpacked_dir());
	ck_assert_str_eq(PTH(SUFFIX_PKG_DOWNLOAD_DIR), pkg_download_dir());
	ck_assert_str_eq(PTH(SUFFIX_DIR_OPKG_COLLIDED), opkg_collided_dir());
#undef PTH
	free(cwd);
}
END_TEST

START_TEST(tilde_paths) {
	struct passwd *pw = getpwuid(getuid());
	set_root_dir("~/updater-root");
#define PTH(SUFFIX) aprintf("%s/updater-root/%s", pw->pw_dir, SUFFIX)
	ck_assert_str_eq(PTH(""), root_dir());
	ck_assert_str_eq(PTH(SUFFIX_STATUS_FILE), status_file());
	ck_assert_str_eq(PTH(SUFFIX_INFO_DIR), info_dir());
	ck_assert_str_eq(PTH(SUFFIX_PKG_UNPACKED_DIR), pkg_unpacked_dir());
	ck_assert_str_eq(PTH(SUFFIX_PKG_DOWNLOAD_DIR), pkg_download_dir());
	ck_assert_str_eq(PTH(SUFFIX_DIR_OPKG_COLLIDED), opkg_collided_dir());
#undef ABS_ROOT
}
END_TEST

void sysinfo_setup_omnia() {
	set_root_dir(aprintf("%s/sysinfo_root/omnia", get_datadir()));
	system_detect();
}

START_TEST(os_release_omnia) {
	ck_assert_str_eq("TurrisOS", os_release(OS_RELEASE_NAME));
	ck_assert_str_eq("4.0", os_release(OS_RELEASE_VERSION));
	ck_assert_str_eq("turrisos", os_release(OS_RELEASE_ID));
	ck_assert_str_eq("TurrisOS 4.0", os_release(OS_RELEASE_PRETTY_NAME));
}
END_TEST

void sysinfo_setup_mox() {
	set_root_dir(aprintf("%s/sysinfo_root/mox", get_datadir()));
	system_detect();
}

START_TEST(os_release_mox) {
	ck_assert_str_eq("TurrisOS", os_release(OS_RELEASE_NAME));
	ck_assert_str_eq("4.0-alpha2", os_release(OS_RELEASE_VERSION));
	ck_assert_str_eq("turrisos", os_release(OS_RELEASE_ID));
	ck_assert_str_eq("TurrisOS 4.0-alpha2", os_release(OS_RELEASE_PRETTY_NAME));
}
END_TEST


Suite *gen_test_suite(void) {
	Suite *result = suite_create("Syscnf");
	TCase *paths = tcase_create("patsh");
	tcase_add_checked_fixture(paths, NULL, paths_teardown);
	tcase_add_test(paths, default_paths);
	tcase_add_test(paths, absolute_paths);
	tcase_add_test(paths, relative_paths);
	tcase_add_test(paths, tilde_paths);
	suite_add_tcase(result, paths);
	TCase *sysinfo_omnia = tcase_create("sysinfo-omnia");
	tcase_add_checked_fixture(sysinfo_omnia, sysinfo_setup_omnia, paths_teardown);
	tcase_add_test(sysinfo_omnia, os_release_omnia);
	suite_add_tcase(result, sysinfo_omnia);
	TCase *sysinfo_mox = tcase_create("sysinfo-mox");
	tcase_add_checked_fixture(sysinfo_mox, sysinfo_setup_mox, paths_teardown);
	tcase_add_test(sysinfo_mox, os_release_mox);
	suite_add_tcase(result, sysinfo_mox);
	return result;
}
