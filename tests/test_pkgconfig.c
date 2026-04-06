/*
 * test_pkgconfig.c — Tests for package/feature detection.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define PC_PATH "/tmp/neo_test_output.pc"

TEST(check_header_stdio)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_header("stdio.h");
    ASSERT_TRUE(found);
}

TEST(check_header_stdlib)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_header("stdlib.h");
    ASSERT_TRUE(found);
}

TEST(check_header_nonexistent)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_header("nonexistent_header_xyz123.h");
    ASSERT_FALSE(found);
}

TEST(check_header_null)
{
    bool found = neo_check_header(NULL);
    ASSERT_FALSE(found);
}

TEST(check_lib_m)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_lib("m");
    ASSERT_TRUE(found);
}

TEST(check_lib_nonexistent)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_lib("nonexistent_lib_xyz987");
    ASSERT_FALSE(found);
}

TEST(check_lib_null)
{
    bool found = neo_check_lib(NULL);
    ASSERT_FALSE(found);
}

TEST(check_symbol_sqrt)
{
    neo_set_verbosity(NEO_QUIET);
    bool found = neo_check_symbol("m", "sqrt");
    ASSERT_TRUE(found);
}

TEST(generate_pkg_config)
{
    neo_set_verbosity(NEO_QUIET);
    bool ok = neo_generate_pkg_config(PC_PATH, "mylib", "1.0.0",
                                      "A test library",
                                      "-I${includedir}/mylib",
                                      "-L${libdir} -lmylib");
    ASSERT_TRUE(ok);

    /* Verify file exists */
    struct stat st;
    ASSERT_EQ(stat(PC_PATH, &st), 0);
    ASSERT_GT(st.st_size, 0);

    /* Read and verify content */
    FILE *f = fopen(PC_PATH, "r");
    ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(buf, "Name: mylib") != NULL);
    ASSERT_TRUE(strstr(buf, "Version: 1.0.0") != NULL);
    ASSERT_TRUE(strstr(buf, "Description: A test library") != NULL);
    ASSERT_TRUE(strstr(buf, "Cflags:") != NULL);
    ASSERT_TRUE(strstr(buf, "Libs:") != NULL);
    ASSERT_TRUE(strstr(buf, "prefix=") != NULL);

    unlink(PC_PATH);
}

TEST(generate_pkg_config_null)
{
    bool ok = neo_generate_pkg_config(NULL, "foo", NULL, NULL, NULL, NULL);
    ASSERT_FALSE(ok);

    ok = neo_generate_pkg_config(PC_PATH, NULL, NULL, NULL, NULL, NULL);
    ASSERT_FALSE(ok);
}

TEST(generate_pkg_config_minimal)
{
    neo_set_verbosity(NEO_QUIET);
    bool ok = neo_generate_pkg_config(PC_PATH, "minimal", NULL, NULL, NULL, NULL);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat(PC_PATH, &st), 0);

    FILE *f = fopen(PC_PATH, "r");
    ASSERT_NOT_NULL(f);
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(buf, "Name: minimal") != NULL);

    unlink(PC_PATH);
}

TEST(find_package_zlib)
{
    neo_set_verbosity(NEO_QUIET);

    /* Check if pkg-config is available first */
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) {
        SKIP_TEST(find_package_zlib, "could not create command");
        return;
    }
    neocmd_append(cmd, "pkg-config", "--version");
    int status = -1, code = -1;
    neo_set_dry_run(false);
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    neocmd_delete(cmd);

    if (!ok || status != 0) {
        SKIP_TEST(find_package_zlib, "pkg-config not available");
        return;
    }

    neo_package_t pkg = neo_find_package("zlib");
    if (!pkg.found) {
        neo_package_free(&pkg);
        SKIP_TEST(find_package_zlib, "zlib not installed");
        return;
    }

    /* If found, it should have libs */
    ASSERT_NOT_NULL(pkg.libs);
    ASSERT_TRUE(strlen(pkg.libs) > 0);
    ASSERT_NOT_NULL(pkg.version);

    neo_package_free(&pkg);
}

TEST(find_package_nonexistent)
{
    neo_set_verbosity(NEO_QUIET);
    neo_package_t pkg = neo_find_package("nonexistent_package_abc789");
    /* Should not be found */
    ASSERT_FALSE(pkg.found);
    neo_package_free(&pkg);
}

TEST(package_free_null)
{
    /* Should not crash */
    neo_package_free(NULL);
}

TEST_MAIN_BEGIN("Package Detection")
    RUN_TEST(check_header_stdio);
    RUN_TEST(check_header_stdlib);
    RUN_TEST(check_header_nonexistent);
    RUN_TEST(check_header_null);
    RUN_TEST(check_lib_m);
    RUN_TEST(check_lib_nonexistent);
    RUN_TEST(check_lib_null);
    RUN_TEST(check_symbol_sqrt);
    RUN_TEST(generate_pkg_config);
    RUN_TEST(generate_pkg_config_null);
    RUN_TEST(generate_pkg_config_minimal);
    RUN_TEST(find_package_zlib);
    RUN_TEST(find_package_nonexistent);
    RUN_TEST(package_free_null);
TEST_MAIN_END
