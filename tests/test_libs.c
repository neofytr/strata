/*
 * test_libs.c — Tests for library building (static and shared).
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_BUILD_DIR "/tmp/neo_test_libs_build/"
#define FIXTURE_DIR    "tests/fixtures/"

static void cleanup_test_files(void)
{
    /* Clean up build artifacts */
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (cmd) {
        neocmd_append(cmd, "rm", "-rf", TEST_BUILD_DIR,
                      "/tmp/neo_test_libfoo.a", "/tmp/neo_test_libbar.so");
        neocmd_run_sync(cmd, NULL, NULL, false);
        neocmd_delete(cmd);
    }
}

TEST(build_static_lib)
{
    cleanup_test_files();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(TEST_BUILD_DIR);

    const char *sources[] = {FIXTURE_DIR "lib.c"};
    bool ok = neo_build_static_lib(NEO_GCC, "/tmp/neo_test_libfoo.a",
                                   sources, 1, NULL);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat("/tmp/neo_test_libfoo.a", &st), 0);
    ASSERT_GT(st.st_size, 0);

    cleanup_test_files();
    neo_set_build_dir(NULL);
}

TEST(build_shared_lib)
{
    cleanup_test_files();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(TEST_BUILD_DIR);

    const char *sources[] = {FIXTURE_DIR "lib.c"};
    bool ok = neo_build_shared_lib(NEO_GCC, "/tmp/neo_test_libbar.so",
                                   sources, 1, NULL, NULL);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat("/tmp/neo_test_libbar.so", &st), 0);
    ASSERT_GT(st.st_size, 0);

    cleanup_test_files();
    neo_set_build_dir(NULL);
}

TEST(build_static_lib_null_args)
{
    bool ok = neo_build_static_lib(NEO_GCC, NULL, NULL, 0, NULL);
    ASSERT_FALSE(ok);
}

TEST(build_shared_lib_null_args)
{
    bool ok = neo_build_shared_lib(NEO_GCC, NULL, NULL, 0, NULL, NULL);
    ASSERT_FALSE(ok);
}

TEST(build_static_lib_dry_run)
{
    neo_set_dry_run(true);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(TEST_BUILD_DIR);

    const char *sources[] = {FIXTURE_DIR "lib.c"};
    bool ok = neo_build_static_lib(NEO_GCC, "/tmp/neo_test_libdry.a",
                                   sources, 1, NULL);
    ASSERT_TRUE(ok);

    /* In dry-run mode the file should NOT be created */
    struct stat st;
    ASSERT_NEQ(stat("/tmp/neo_test_libdry.a", &st), 0);

    neo_set_dry_run(false);
    neo_set_build_dir(NULL);
}

TEST_MAIN_BEGIN("Library Building")
    RUN_TEST(build_static_lib);
    RUN_TEST(build_shared_lib);
    RUN_TEST(build_static_lib_null_args);
    RUN_TEST(build_shared_lib_null_args);
    RUN_TEST(build_static_lib_dry_run);
TEST_MAIN_END
