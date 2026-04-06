/*
 * test_graph.c — Tests for the target graph.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define DOT_PATH "/tmp/neo_test_graph.dot"

TEST(graph_create_destroy)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);
    neo_graph_destroy(g);
}

TEST(graph_destroy_null)
{
    /* Should not crash */
    neo_graph_destroy(NULL);
}

TEST(graph_add_executable)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"main.c"};
    neo_target_t *t = neo_add_executable(g, "myapp", srcs, 1);
    ASSERT_NOT_NULL(t);

    neo_graph_destroy(g);
}

TEST(graph_add_static_lib)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"lib.c", "util.c"};
    neo_target_t *t = neo_add_static_lib(g, "libfoo.a", srcs, 2);
    ASSERT_NOT_NULL(t);

    neo_graph_destroy(g);
}

TEST(graph_add_shared_lib)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"lib.c"};
    neo_target_t *t = neo_add_shared_lib(g, "libbar.so", srcs, 1);
    ASSERT_NOT_NULL(t);

    neo_graph_destroy(g);
}

TEST(graph_add_custom)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    neo_target_t *t = neo_add_custom(g, "generate", "echo generating");
    ASSERT_NOT_NULL(t);

    neo_graph_destroy(g);
}

TEST(graph_find_target)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"a.c"};
    neo_add_executable(g, "alpha", srcs, 1);
    neo_add_executable(g, "beta", srcs, 1);
    neo_add_executable(g, "gamma", srcs, 1);

    neo_target_t *found = neo_graph_find(g, "beta");
    ASSERT_NOT_NULL(found);

    neo_target_t *missing = neo_graph_find(g, "delta");
    ASSERT_NULL(missing);

    neo_graph_destroy(g);
}

TEST(graph_find_null)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    ASSERT_NULL(neo_graph_find(g, NULL));
    ASSERT_NULL(neo_graph_find(NULL, "foo"));

    neo_graph_destroy(g);
}

TEST(graph_add_dependencies)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *lib_srcs[] = {"lib.c"};
    const char *app_srcs[] = {"main.c"};

    neo_target_t *lib = neo_add_static_lib(g, "libutil.a", lib_srcs, 1);
    neo_target_t *app = neo_add_executable(g, "app", app_srcs, 1);
    ASSERT_NOT_NULL(lib);
    ASSERT_NOT_NULL(app);

    neo_target_depends_on(app, lib);

    /* No crash = success; the dependency is stored internally */
    neo_graph_destroy(g);
}

TEST(graph_diamond_deps)
{
    /*
     * Diamond dependency:
     *     D
     *    / \
     *   B   C
     *    \ /
     *     A
     */
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"x.c"};
    neo_target_t *a = neo_add_static_lib(g, "libA.a", srcs, 1);
    neo_target_t *b = neo_add_static_lib(g, "libB.a", srcs, 1);
    neo_target_t *c = neo_add_static_lib(g, "libC.a", srcs, 1);
    neo_target_t *d = neo_add_executable(g, "appD", srcs, 1);

    neo_target_depends_on(b, a);
    neo_target_depends_on(c, a);
    neo_target_depends_on(d, b);
    neo_target_depends_on(d, c);

    /* Build in dry-run mode to verify toposort works */
    neo_set_dry_run(true);
    neo_set_verbosity(NEO_QUIET);
    int built = neo_graph_build(g);
    ASSERT_EQ(built, 4);

    neo_set_dry_run(false);
    neo_graph_destroy(g);
}

TEST(graph_build_dry_run)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"foo.c"};
    neo_add_executable(g, "foo", srcs, 1);

    neo_set_dry_run(true);
    neo_set_verbosity(NEO_QUIET);
    int built = neo_graph_build(g);
    ASSERT_EQ(built, 1);

    neo_set_dry_run(false);
    neo_graph_destroy(g);
}

TEST(graph_build_empty)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    int built = neo_graph_build(g);
    ASSERT_EQ(built, 0);

    neo_graph_destroy(g);
}

TEST(graph_target_set_properties)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"main.c"};
    neo_target_t *t = neo_add_executable(g, "myapp", srcs, 1);
    ASSERT_NOT_NULL(t);

    neo_target_set_compiler(t, NEO_CLANG);
    neo_target_add_cflags(t, "-Wall");
    neo_target_add_ldflags(t, "-lm");
    neo_target_add_include_dir(t, "/usr/include");
    neo_target_set_version(t, 1, 2, 3);

    /* No crash = success */
    neo_graph_destroy(g);
}

TEST(graph_export_dot)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    const char *srcs[] = {"src.c"};
    neo_target_t *lib = neo_add_static_lib(g, "libcore.a", srcs, 1);
    neo_target_t *app = neo_add_executable(g, "myapp", srcs, 1);
    neo_target_depends_on(app, lib);

    neo_set_verbosity(NEO_QUIET);
    bool ok = neo_graph_export_dot(g, DOT_PATH);
    ASSERT_TRUE(ok);

    /* Verify file exists and has content */
    struct stat st;
    ASSERT_EQ(stat(DOT_PATH, &st), 0);
    ASSERT_GT(st.st_size, 0);

    /* Read and check for expected content */
    FILE *f = fopen(DOT_PATH, "r");
    ASSERT_NOT_NULL(f);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    ASSERT_TRUE(strstr(buf, "digraph neobuild") != NULL);
    ASSERT_TRUE(strstr(buf, "myapp") != NULL);
    ASSERT_TRUE(strstr(buf, "libcore.a") != NULL);

    unlink(DOT_PATH);
    neo_graph_destroy(g);
}

TEST(graph_export_dot_null)
{
    ASSERT_FALSE(neo_graph_export_dot(NULL, DOT_PATH));

    neo_graph_t *g = neo_graph_create();
    ASSERT_FALSE(neo_graph_export_dot(g, NULL));
    neo_graph_destroy(g);
}

TEST(graph_enable_content_hash)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    neo_graph_enable_content_hash(g);
    /* No crash = success */

    neo_graph_destroy(g);
}

TEST(graph_enable_ccache)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    neo_graph_enable_ccache(g);
    /* No crash = success */

    neo_graph_destroy(g);
}

TEST(graph_set_toolchain)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
    ASSERT_NOT_NULL(tc);

    neo_graph_set_toolchain(g, tc);
    /* No crash = success */

    neo_toolchain_destroy(tc);
    neo_graph_destroy(g);
}

TEST_MAIN_BEGIN("Target Graph")
    RUN_TEST(graph_create_destroy);
    RUN_TEST(graph_destroy_null);
    RUN_TEST(graph_add_executable);
    RUN_TEST(graph_add_static_lib);
    RUN_TEST(graph_add_shared_lib);
    RUN_TEST(graph_add_custom);
    RUN_TEST(graph_find_target);
    RUN_TEST(graph_find_null);
    RUN_TEST(graph_add_dependencies);
    RUN_TEST(graph_diamond_deps);
    RUN_TEST(graph_build_dry_run);
    RUN_TEST(graph_build_empty);
    RUN_TEST(graph_target_set_properties);
    RUN_TEST(graph_export_dot);
    RUN_TEST(graph_export_dot_null);
    RUN_TEST(graph_enable_content_hash);
    RUN_TEST(graph_enable_ccache);
    RUN_TEST(graph_set_toolchain);
TEST_MAIN_END
