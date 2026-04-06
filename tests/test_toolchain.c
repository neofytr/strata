/*
 * test_toolchain.c — Tests for toolchain / cross-compilation support.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <string.h>

TEST(toolchain_create_destroy)
{
    neo_toolchain_t *tc = neo_toolchain_create("test-");
    ASSERT_NOT_NULL(tc);
    neo_toolchain_destroy(tc);
}

TEST(toolchain_create_null_prefix)
{
    neo_toolchain_t *tc = neo_toolchain_create(NULL);
    ASSERT_NOT_NULL(tc);
    neo_toolchain_destroy(tc);
}

TEST(toolchain_set_sysroot)
{
    neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
    ASSERT_NOT_NULL(tc);

    neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
    /* No crash = success; sysroot is stored internally */

    /* Set to NULL to clear */
    neo_toolchain_set_sysroot(tc, NULL);

    neo_toolchain_destroy(tc);
}

TEST(toolchain_set_cc)
{
    neo_toolchain_t *tc = neo_toolchain_create("riscv64-unknown-linux-gnu-");
    ASSERT_NOT_NULL(tc);

    neo_toolchain_set_cc(tc, "riscv64-unknown-linux-gnu-gcc");
    /* No crash = success */

    neo_toolchain_destroy(tc);
}

TEST(toolchain_set_cxx)
{
    neo_toolchain_t *tc = neo_toolchain_create("aarch64-linux-gnu-");
    ASSERT_NOT_NULL(tc);

    neo_toolchain_set_cxx(tc, "aarch64-linux-gnu-g++");
    /* No crash = success */

    neo_toolchain_destroy(tc);
}

TEST(toolchain_set_all)
{
    neo_toolchain_t *tc = neo_toolchain_create("mips-linux-gnu-");
    ASSERT_NOT_NULL(tc);

    neo_toolchain_set_sysroot(tc, "/opt/mips-sysroot");
    neo_toolchain_set_cc(tc, "mips-linux-gnu-gcc");
    neo_toolchain_set_cxx(tc, "mips-linux-gnu-g++");

    /* Replace values */
    neo_toolchain_set_sysroot(tc, "/opt/mips-sysroot-v2");
    neo_toolchain_set_cc(tc, "mips-linux-gnu-gcc-12");

    neo_toolchain_destroy(tc);
}

TEST(toolchain_destroy_null)
{
    /* Should not crash */
    neo_toolchain_destroy(NULL);
}

TEST(toolchain_set_on_null)
{
    /* All setters should handle NULL toolchain gracefully */
    neo_toolchain_set_sysroot(NULL, "/foo");
    neo_toolchain_set_cc(NULL, "gcc");
    neo_toolchain_set_cxx(NULL, "g++");
}

TEST(toolchain_with_graph)
{
    neo_graph_t *g = neo_graph_create();
    ASSERT_NOT_NULL(g);

    neo_toolchain_t *tc = neo_toolchain_create("arm-none-eabi-");
    ASSERT_NOT_NULL(tc);
    neo_toolchain_set_sysroot(tc, "/opt/arm-none-eabi");

    neo_graph_set_toolchain(g, tc);

    const char *srcs[] = {"main.c"};
    neo_target_t *t = neo_add_executable(g, "firmware", srcs, 1);
    ASSERT_NOT_NULL(t);

    /* Per-target toolchain override */
    neo_toolchain_t *tc2 = neo_toolchain_create("xtensa-esp32-elf-");
    ASSERT_NOT_NULL(tc2);
    neo_target_set_toolchain(t, tc2);

    neo_toolchain_destroy(tc);
    neo_toolchain_destroy(tc2);
    neo_graph_destroy(g);
}

TEST_MAIN_BEGIN("Toolchain")
    RUN_TEST(toolchain_create_destroy);
    RUN_TEST(toolchain_create_null_prefix);
    RUN_TEST(toolchain_set_sysroot);
    RUN_TEST(toolchain_set_cc);
    RUN_TEST(toolchain_set_cxx);
    RUN_TEST(toolchain_set_all);
    RUN_TEST(toolchain_destroy_null);
    RUN_TEST(toolchain_set_on_null);
    RUN_TEST(toolchain_with_graph);
TEST_MAIN_END
