/*
 * test_install.c — Tests for install functionality.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define INSTALL_PREFIX "/tmp/neo_test_install"

static void cleanup_install_dir(void)
{
    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (cmd) {
        neocmd_append(cmd, "rm", "-rf", INSTALL_PREFIX);
        neocmd_run_sync(cmd, NULL, NULL, false);
        neocmd_delete(cmd);
    }
}

TEST(install_dirs_default)
{
    neo_install_dirs_t d = neo_install_dirs_default(NULL);
    ASSERT_STR_EQ(d.prefix, "/usr/local");
    ASSERT_STR_EQ(d.bindir, "/usr/local/bin");
    ASSERT_STR_EQ(d.libdir, "/usr/local/lib");
    ASSERT_STR_EQ(d.includedir, "/usr/local/include");
    ASSERT_STR_EQ(d.pkgconfigdir, "/usr/local/lib/pkgconfig");
}

TEST(install_dirs_custom_prefix)
{
    neo_install_dirs_t d = neo_install_dirs_default("/opt/myapp");
    ASSERT_STR_EQ(d.prefix, "/opt/myapp");
    ASSERT_STR_EQ(d.bindir, "/opt/myapp/bin");
    ASSERT_STR_EQ(d.libdir, "/opt/myapp/lib");
    ASSERT_STR_EQ(d.includedir, "/opt/myapp/include");
    ASSERT_STR_EQ(d.pkgconfigdir, "/opt/myapp/lib/pkgconfig");
}

TEST(install_dirs_tmp_prefix)
{
    neo_install_dirs_t d = neo_install_dirs_default(INSTALL_PREFIX);
    ASSERT_STR_EQ(d.prefix, INSTALL_PREFIX);
    ASSERT_STR_EQ(d.bindir, INSTALL_PREFIX "/bin");
    ASSERT_STR_EQ(d.libdir, INSTALL_PREFIX "/lib");
    ASSERT_STR_EQ(d.includedir, INSTALL_PREFIX "/include");
}

TEST(install_headers)
{
    cleanup_install_dir();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    neo_install_dirs_t dirs = neo_install_dirs_default(INSTALL_PREFIX);

    const char *headers[] = {"tests/fixtures/lib.h"};
    bool ok = neo_install_headers(headers, 1, NULL, &dirs);
    ASSERT_TRUE(ok);

    /* Verify the header was copied */
    char expected_path[512];
    snprintf(expected_path, sizeof(expected_path), "%s/lib.h", dirs.includedir);
    struct stat st;
    ASSERT_EQ(stat(expected_path, &st), 0);
    ASSERT_GT(st.st_size, 0);

    cleanup_install_dir();
}

TEST(install_headers_with_subdir)
{
    cleanup_install_dir();

    neo_set_dry_run(false);
    neo_set_verbosity(NEO_QUIET);

    neo_install_dirs_t dirs = neo_install_dirs_default(INSTALL_PREFIX);

    const char *headers[] = {"tests/fixtures/lib.h"};
    bool ok = neo_install_headers(headers, 1, "mylib", &dirs);
    ASSERT_TRUE(ok);

    /* Verify the header was copied to subdir */
    char expected_path[512];
    snprintf(expected_path, sizeof(expected_path), "%s/mylib/lib.h", dirs.includedir);
    struct stat st;
    ASSERT_EQ(stat(expected_path, &st), 0);

    cleanup_install_dir();
}

TEST(install_headers_null)
{
    neo_install_dirs_t dirs = neo_install_dirs_default(INSTALL_PREFIX);
    bool ok = neo_install_headers(NULL, 0, NULL, &dirs);
    ASSERT_FALSE(ok);

    const char *headers[] = {"test.h"};
    ok = neo_install_headers(headers, 1, NULL, NULL);
    ASSERT_FALSE(ok);
}

TEST(install_target_null)
{
    bool ok = neo_install_target(NULL, "foo", NULL);
    ASSERT_FALSE(ok);

    neo_graph_t *g = neo_graph_create();
    ok = neo_install_target(g, NULL, NULL);
    ASSERT_FALSE(ok);
    neo_graph_destroy(g);
}

TEST_MAIN_BEGIN("Install")
    RUN_TEST(install_dirs_default);
    RUN_TEST(install_dirs_custom_prefix);
    RUN_TEST(install_dirs_tmp_prefix);
    RUN_TEST(install_headers);
    RUN_TEST(install_headers_with_subdir);
    RUN_TEST(install_headers_null);
    RUN_TEST(install_target_null);
TEST_MAIN_END
