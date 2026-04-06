/*
 * test_arena.c — Tests for the neo_arena allocator.
 */

#include "test_harness.h"
#include "buildsysdep/neobuild.h"

#include <stdint.h>
#include <string.h>

TEST(arena_create_destroy)
{
    neo_arena_t *a = neo_arena_create(0);
    ASSERT_NOT_NULL(a);
    neo_arena_destroy(a);
}

TEST(arena_create_custom_page_size)
{
    neo_arena_t *a = neo_arena_create(1024);
    ASSERT_NOT_NULL(a);
    neo_arena_destroy(a);
}

TEST(arena_alloc_small_blocks)
{
    neo_arena_t *a = neo_arena_create(4096);
    ASSERT_NOT_NULL(a);

    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = neo_arena_alloc(a, 16);
        ASSERT_NOT_NULL(ptrs[i]);
    }

    /* Verify no two allocations overlap by checking they are distinct */
    for (int i = 0; i < 99; i++) {
        ASSERT_TRUE(ptrs[i] != ptrs[i + 1]);
    }

    neo_arena_destroy(a);
}

TEST(arena_alloc_alignment)
{
    neo_arena_t *a = neo_arena_create(4096);
    ASSERT_NOT_NULL(a);

    /* Allocate odd sizes and verify 8-byte alignment */
    for (int i = 1; i <= 33; i++) {
        void *p = neo_arena_alloc(a, (size_t)i);
        ASSERT_NOT_NULL(p);
        ASSERT_EQ((uintptr_t)p % 8, 0);
    }

    neo_arena_destroy(a);
}

TEST(arena_alloc_zero_returns_null)
{
    neo_arena_t *a = neo_arena_create(0);
    ASSERT_NOT_NULL(a);

    void *p = neo_arena_alloc(a, 0);
    ASSERT_NULL(p);

    neo_arena_destroy(a);
}

TEST(arena_strdup)
{
    neo_arena_t *a = neo_arena_create(0);
    ASSERT_NOT_NULL(a);

    const char *original = "Hello, neobuild!";
    char *copy = neo_arena_strdup(a, original);
    ASSERT_NOT_NULL(copy);
    ASSERT_STR_EQ(copy, original);
    ASSERT_TRUE(copy != original);

    /* Empty string */
    char *empty = neo_arena_strdup(a, "");
    ASSERT_NOT_NULL(empty);
    ASSERT_STR_EQ(empty, "");

    /* NULL returns NULL */
    char *null_dup = neo_arena_strdup(a, NULL);
    ASSERT_NULL(null_dup);

    neo_arena_destroy(a);
}

TEST(arena_sprintf)
{
    neo_arena_t *a = neo_arena_create(0);
    ASSERT_NOT_NULL(a);

    char *s = neo_arena_sprintf(a, "value=%d, name=%s", 42, "test");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "value=42, name=test");

    /* Empty format */
    char *e = neo_arena_sprintf(a, "%s", "");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e, "");

    neo_arena_destroy(a);
}

TEST(arena_oversized_alloc)
{
    /* Create a small page, then allocate something bigger */
    neo_arena_t *a = neo_arena_create(64);
    ASSERT_NOT_NULL(a);

    void *big = neo_arena_alloc(a, 1024);
    ASSERT_NOT_NULL(big);
    ASSERT_EQ((uintptr_t)big % 8, 0);

    /* Can still do small allocs after */
    void *small = neo_arena_alloc(a, 8);
    ASSERT_NOT_NULL(small);

    neo_arena_destroy(a);
}

TEST(arena_many_strings)
{
    neo_arena_t *a = neo_arena_create(256);
    ASSERT_NOT_NULL(a);

    /* Force multiple page allocations */
    for (int i = 0; i < 200; i++) {
        char *s = neo_arena_sprintf(a, "string_number_%d_with_extra_padding", i);
        ASSERT_NOT_NULL(s);
        char expected[64];
        snprintf(expected, sizeof(expected), "string_number_%d_with_extra_padding", i);
        ASSERT_STR_EQ(s, expected);
    }

    neo_arena_destroy(a);
}

TEST(arena_destroy_null)
{
    /* Should not crash */
    neo_arena_destroy(NULL);
}

TEST_MAIN_BEGIN("Arena Allocator")
    RUN_TEST(arena_create_destroy);
    RUN_TEST(arena_create_custom_page_size);
    RUN_TEST(arena_alloc_small_blocks);
    RUN_TEST(arena_alloc_alignment);
    RUN_TEST(arena_alloc_zero_returns_null);
    RUN_TEST(arena_strdup);
    RUN_TEST(arena_sprintf);
    RUN_TEST(arena_oversized_alloc);
    RUN_TEST(arena_many_strings);
    RUN_TEST(arena_destroy_null);
TEST_MAIN_END
