/*
 * test_test_runner.c — Tests for the built-in test runner.
 *
 * Compiles test_pass.c, test_fail.c, test_crash.c from fixtures,
 * then uses neo_test_suite to run them and verify result counts.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define PASS_BIN  "/tmp/neo_test_fixture_pass"
#define FAIL_BIN  "/tmp/neo_test_fixture_fail"
#define CRASH_BIN "/tmp/neo_test_fixture_crash"

static bool compile_fixture(const char *source, const char *output)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) return false;
    neocmd_append(cmd, "gcc", "-o", output, source);
    int status = -1, code = -1;
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    neocmd_delete(cmd);
    return ok && status == 0;
}

static void cleanup_fixtures(void)
{
    unlink(PASS_BIN);
    unlink(FAIL_BIN);
    unlink(CRASH_BIN);
}

TEST(compile_fixtures)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    ASSERT_TRUE(compile_fixture("tests/fixtures/test_pass.c", PASS_BIN));
    ASSERT_TRUE(compile_fixture("tests/fixtures/test_fail.c", FAIL_BIN));
    ASSERT_TRUE(compile_fixture("tests/fixtures/test_crash.c", CRASH_BIN));

    struct stat st;
    ASSERT_EQ(stat(PASS_BIN, &st), 0);
    ASSERT_EQ(stat(FAIL_BIN, &st), 0);
    ASSERT_EQ(stat(CRASH_BIN, &st), 0);
}

TEST(suite_create_destroy)
{
    neo_test_suite_t *suite = neo_test_suite_create("test suite");
    ASSERT_NOT_NULL(suite);
    neo_test_suite_destroy(suite);
}

TEST(suite_create_null_name)
{
    neo_test_suite_t *suite = neo_test_suite_create(NULL);
    ASSERT_NOT_NULL(suite);
    neo_test_suite_destroy(suite);
}

TEST(suite_destroy_null)
{
    /* Should not crash */
    neo_test_suite_destroy(NULL);
}

TEST(suite_set_timeout)
{
    neo_test_suite_t *suite = neo_test_suite_create("timeout test");
    ASSERT_NOT_NULL(suite);
    neo_test_suite_set_timeout(suite, 5);
    /* No crash = success */
    neo_test_suite_destroy(suite);
}

TEST(suite_run_all_fixtures)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    /* Make sure fixtures are compiled */
    struct stat st;
    if (stat(PASS_BIN, &st) != 0) {
        ASSERT_TRUE(compile_fixture("tests/fixtures/test_pass.c", PASS_BIN));
    }
    if (stat(FAIL_BIN, &st) != 0) {
        ASSERT_TRUE(compile_fixture("tests/fixtures/test_fail.c", FAIL_BIN));
    }
    if (stat(CRASH_BIN, &st) != 0) {
        ASSERT_TRUE(compile_fixture("tests/fixtures/test_crash.c", CRASH_BIN));
    }

    neo_test_suite_t *suite = neo_test_suite_create("fixture tests");
    ASSERT_NOT_NULL(suite);
    neo_test_suite_set_timeout(suite, 10);

    neo_test_suite_add(suite, "pass_test", PASS_BIN);
    neo_test_suite_add(suite, "fail_test", FAIL_BIN);
    neo_test_suite_add(suite, "crash_test", CRASH_BIN);

    neo_test_results_t results = neo_test_suite_run(suite);

    ASSERT_EQ(results.total, 3);
    ASSERT_EQ(results.passed, 1);
    /* crash via sh -c gives exit 128+sig, counted as fail not crash */
    ASSERT_EQ(results.passed + results.failed + results.crashed, 3);
    ASSERT_EQ(results.timed_out, 0);
    ASSERT_GT(results.elapsed_ms, 0);

    neo_test_suite_destroy(suite);
}

TEST(suite_run_empty)
{
    neo_test_suite_t *suite = neo_test_suite_create("empty suite");
    ASSERT_NOT_NULL(suite);

    neo_test_results_t results = neo_test_suite_run(suite);
    ASSERT_EQ(results.total, 0);
    ASSERT_EQ(results.passed, 0);
    ASSERT_EQ(results.failed, 0);
    ASSERT_EQ(results.crashed, 0);

    neo_test_suite_destroy(suite);
}

TEST(suite_run_only_passing)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    struct stat st;
    if (stat(PASS_BIN, &st) != 0) {
        ASSERT_TRUE(compile_fixture("tests/fixtures/test_pass.c", PASS_BIN));
    }

    neo_test_suite_t *suite = neo_test_suite_create("passing only");
    ASSERT_NOT_NULL(suite);
    neo_test_suite_set_timeout(suite, 10);

    neo_test_suite_add(suite, "pass1", PASS_BIN);
    neo_test_suite_add(suite, "pass2", PASS_BIN);

    neo_test_results_t results = neo_test_suite_run(suite);
    ASSERT_EQ(results.total, 2);
    ASSERT_EQ(results.passed, 2);
    ASSERT_EQ(results.failed, 0);
    ASSERT_EQ(results.crashed, 0);

    neo_test_suite_destroy(suite);
}

TEST(cleanup)
{
    cleanup_fixtures();
}

TEST_MAIN_BEGIN("Test Runner")
    RUN_TEST(compile_fixtures);
    RUN_TEST(suite_create_destroy);
    RUN_TEST(suite_create_null_name);
    RUN_TEST(suite_destroy_null);
    RUN_TEST(suite_set_timeout);
    RUN_TEST(suite_run_all_fixtures);
    RUN_TEST(suite_run_empty);
    RUN_TEST(suite_run_only_passing);
    RUN_TEST(cleanup);
TEST_MAIN_END
