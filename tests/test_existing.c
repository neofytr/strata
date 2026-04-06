/*
 * test_existing.c — Regression tests for existing API surface.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define TEST_DIR     "/tmp/neo_test_existing_dir"
#define TEST_SUBDIR  "/tmp/neo_test_existing_dir/a/b/c"
#define TEST_CC_JSON "/tmp/neo_test_compile_commands.json"
#define TEST_OBJ     "/tmp/neo_test_existing_obj.o"
#define TEST_BUILD   "/tmp/neo_test_existing_build/"

static void cleanup(void)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (cmd) {
        neocmd_append(cmd, "rm", "-rf", TEST_DIR, TEST_CC_JSON, TEST_OBJ, TEST_BUILD);
        neocmd_run_sync(cmd, NULL, NULL, false);
        neocmd_delete(cmd);
    }
}

TEST(set_verbosity)
{
    neo_set_verbosity(NEO_QUIET);
    neo_set_verbosity(NEO_NORMAL);
    neo_set_verbosity(NEO_VERBOSE);
    neo_set_verbosity(NEO_QUIET);
    /* No crash = success */
}

TEST(set_dry_run)
{
    neo_set_dry_run(true);
    neo_set_dry_run(false);
    /* No crash = success */
}

TEST(set_build_dir)
{
    neo_set_build_dir("/tmp/neo_test_build");
    neo_set_build_dir(NULL);
    neo_set_build_dir("");
    neo_set_build_dir("/tmp/neo_test_build/");
    neo_set_build_dir(NULL);
    /* No crash = success */
}

TEST(set_jobs)
{
    neo_set_jobs(1);
    neo_set_jobs(4);
    neo_set_jobs(0);  /* Should default to nproc */
    neo_set_jobs(-1); /* Should also default */
    neo_set_jobs(1);
    /* No crash = success */
}

TEST(set_profile)
{
    neo_set_profile(NEO_PROFILE_NONE);
    neo_set_profile(NEO_PROFILE_DEBUG);
    neo_set_profile(NEO_PROFILE_RELEASE);
    neo_set_profile(NEO_PROFILE_RELDBG);
    neo_set_profile(NEO_PROFILE_NONE);
    /* No crash = success */
}

TEST(set_global_default_compiler)
{
    neo_set_global_default_compiler(NEO_GCC);
    ASSERT_EQ(neo_get_global_default_compiler(), NEO_GCC);

    neo_set_global_default_compiler(NEO_CLANG);
    ASSERT_EQ(neo_get_global_default_compiler(), NEO_CLANG);

    neo_set_global_default_compiler(NEO_GCC);
}

TEST(mkdir_simple)
{
    cleanup();
    neo_set_verbosity(NEO_QUIET);

    bool ok = neo_mkdir(TEST_DIR, 0755);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat(TEST_DIR, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    cleanup();
}

TEST(mkdir_nested)
{
    cleanup();
    neo_set_verbosity(NEO_QUIET);

    bool ok = neo_mkdir(TEST_SUBDIR, 0755);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat(TEST_SUBDIR, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));

    cleanup();
}

TEST(mkdir_existing)
{
    cleanup();

    neo_mkdir(TEST_DIR, 0755);
    /* Calling again should succeed (EEXIST is ok) */
    bool ok = neo_mkdir(TEST_DIR, 0755);
    ASSERT_TRUE(ok);

    cleanup();
}

TEST(mkdir_null)
{
    bool ok = neo_mkdir(NULL, 0755);
    ASSERT_FALSE(ok);
}

TEST(compile_to_object_dry_run)
{
    cleanup();

    neo_set_dry_run(true);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(NULL);

    bool ok = neo_compile_to_object_file(NEO_GCC, "tests/fixtures/hello.c",
                                         TEST_OBJ, NULL, true);
    ASSERT_TRUE(ok);

    /* In dry-run mode, the object file should NOT be created */
    struct stat st;
    ASSERT_NEQ(stat(TEST_OBJ, &st), 0);

    neo_set_dry_run(false);
    cleanup();
}

TEST(compile_to_object_real)
{
    cleanup();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(NULL);
    neo_set_profile(NEO_PROFILE_NONE);

    bool ok = neo_compile_to_object_file(NEO_GCC, "tests/fixtures/hello.c",
                                         TEST_OBJ, NULL, true);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat(TEST_OBJ, &st), 0);
    ASSERT_GT(st.st_size, 0);

    cleanup();
}

TEST(compile_null_source)
{
    bool ok = neo_compile_to_object_file(NEO_GCC, NULL, TEST_OBJ, NULL, true);
    ASSERT_FALSE(ok);
}

TEST(export_compile_commands)
{
    cleanup();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neo_set_build_dir(NULL);
    neo_set_profile(NEO_PROFILE_NONE);

    /* Compile something to populate the compile_commands accumulator */
    neo_compile_to_object_file(NEO_GCC, "tests/fixtures/hello.c",
                               TEST_OBJ, NULL, true);

    bool ok = neo_export_compile_commands(TEST_CC_JSON);
    ASSERT_TRUE(ok);

    struct stat st;
    ASSERT_EQ(stat(TEST_CC_JSON, &st), 0);
    ASSERT_GT(st.st_size, 0);

    /* Read and verify it looks like JSON */
    FILE *f = fopen(TEST_CC_JSON, "r");
    ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(buf[0] == '[');
    ASSERT_TRUE(strstr(buf, "directory") != NULL);
    ASSERT_TRUE(strstr(buf, "command") != NULL);
    ASSERT_TRUE(strstr(buf, "file") != NULL);

    cleanup();
}

TEST(export_compile_commands_null)
{
    bool ok = neo_export_compile_commands(NULL);
    ASSERT_FALSE(ok);
}

TEST(compat_aliases)
{
    /* Verify backward compatibility aliases compile correctly */
    ASSERT_EQ(LD, NEO_LD);
    ASSERT_EQ(AS, NEO_AS);
    ASSERT_EQ(GCC, NEO_GCC);
    ASSERT_EQ(CLANG, NEO_CLANG);
    ASSERT_EQ(GLOBAL_DEFAULT, NEO_GLOBAL_DEFAULT);

    ASSERT_EQ(ERROR, NEO_LOG_ERROR);
    ASSERT_EQ(WARNING, NEO_LOG_WARNING);
    ASSERT_EQ(INFO, NEO_LOG_INFO);
    ASSERT_EQ(DEBUG, NEO_LOG_DEBUG);

    ASSERT_EQ(DASH, NEO_DASH);
    ASSERT_EQ(BASH, NEO_BASH);
    ASSERT_EQ(SH, NEO_SH);
}

TEST_MAIN_BEGIN("Existing API Regression")
    RUN_TEST(set_verbosity);
    RUN_TEST(set_dry_run);
    RUN_TEST(set_build_dir);
    RUN_TEST(set_jobs);
    RUN_TEST(set_profile);
    RUN_TEST(set_global_default_compiler);
    RUN_TEST(mkdir_simple);
    RUN_TEST(mkdir_nested);
    RUN_TEST(mkdir_existing);
    RUN_TEST(mkdir_null);
    RUN_TEST(compile_to_object_dry_run);
    RUN_TEST(compile_to_object_real);
    RUN_TEST(compile_null_source);
    RUN_TEST(export_compile_commands);
    RUN_TEST(export_compile_commands_null);
    RUN_TEST(compat_aliases);
TEST_MAIN_END
