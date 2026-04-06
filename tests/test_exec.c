/*
 * test_exec.c — Tests for command execution API.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <sys/wait.h>

TEST(cmd_create_sh)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);
    neocmd_delete(cmd);
}

TEST(cmd_create_direct)
{
    neocmd_t *cmd = neocmd_create(NEO_DIRECT);
    ASSERT_NOT_NULL(cmd);
    neocmd_delete(cmd);
}

TEST(cmd_append_and_render)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "echo", "hello", "world");
    const char *rendered = neocmd_render(cmd);
    ASSERT_NOT_NULL(rendered);
    ASSERT_STR_EQ(rendered, "echo hello world");
    free((void *)rendered);

    neocmd_delete(cmd);
}

TEST(cmd_render_single_arg)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "ls");
    const char *rendered = neocmd_render(cmd);
    ASSERT_NOT_NULL(rendered);
    ASSERT_STR_EQ(rendered, "ls");
    free((void *)rendered);

    neocmd_delete(cmd);
}

TEST(cmd_render_empty)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    const char *rendered = neocmd_render(cmd);
    ASSERT_NOT_NULL(rendered);
    /* Should be empty string */
    ASSERT_STR_EQ(rendered, "");
    free((void *)rendered);

    neocmd_delete(cmd);
}

TEST(cmd_run_sync_echo)
{
    /* Make sure dry-run is off */
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "echo", "hello");
    int status = -1, code = -1;
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    ASSERT_TRUE(ok);
    ASSERT_EQ(status, 0);
    ASSERT_EQ(code, CLD_EXITED);

    neocmd_delete(cmd);
}

TEST(cmd_run_sync_direct)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    neocmd_t *cmd = neocmd_create(NEO_DIRECT);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "/bin/echo", "hello");
    int status = -1, code = -1;
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    ASSERT_TRUE(ok);
    ASSERT_EQ(status, 0);
    ASSERT_EQ(code, CLD_EXITED);

    neocmd_delete(cmd);
}

TEST(cmd_run_sync_false)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "false");
    int status = -1, code = -1;
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    ASSERT_TRUE(ok);
    ASSERT_NEQ(status, 0);

    neocmd_delete(cmd);
}

TEST(cmd_dry_run_mode)
{
    neo_set_dry_run(true);
    neo_set_verbosity(NEO_QUIET);

    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "rm", "-rf", "/this/should/not/run");
    int status = -1, code = -1;
    bool ok = neocmd_run_sync(cmd, &status, &code, false);
    ASSERT_TRUE(ok);
    /* In dry-run mode, status should be 0 (simulated success) */
    ASSERT_EQ(status, 0);
    ASSERT_EQ(code, CLD_EXITED);

    neocmd_delete(cmd);
    neo_set_dry_run(false);
}

TEST(cmd_delete_null)
{
    /* Should not crash and should return false */
    bool result = neocmd_delete(NULL);
    ASSERT_FALSE(result);
}

TEST(cmd_render_null)
{
    const char *r = neocmd_render(NULL);
    ASSERT_NULL(r);
}

TEST(cmd_multiple_appends)
{
    neocmd_t *cmd = neocmd_create(NEO_SH);
    ASSERT_NOT_NULL(cmd);

    neocmd_append(cmd, "gcc");
    neocmd_append(cmd, "-c", "main.c");
    neocmd_append(cmd, "-o", "main.o");

    const char *rendered = neocmd_render(cmd);
    ASSERT_NOT_NULL(rendered);
    ASSERT_STR_EQ(rendered, "gcc -c main.c -o main.o");
    free((void *)rendered);

    neocmd_delete(cmd);
}

TEST_MAIN_BEGIN("Command Execution")
    RUN_TEST(cmd_create_sh);
    RUN_TEST(cmd_create_direct);
    RUN_TEST(cmd_append_and_render);
    RUN_TEST(cmd_render_single_arg);
    RUN_TEST(cmd_render_empty);
    RUN_TEST(cmd_run_sync_echo);
    RUN_TEST(cmd_run_sync_direct);
    RUN_TEST(cmd_run_sync_false);
    RUN_TEST(cmd_dry_run_mode);
    RUN_TEST(cmd_delete_null);
    RUN_TEST(cmd_render_null);
    RUN_TEST(cmd_multiple_appends);
TEST_MAIN_END
