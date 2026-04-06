#ifndef NEO_TEST_HARNESS_H
#define NEO_TEST_HARNESS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

static int neo__test_pass = 0;
static int neo__test_fail = 0;
static int neo__test_skip = 0;

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) do { \
    printf("  %-50s ", #name); \
    fflush(stdout); \
    test_##name(); \
    neo__test_pass++; \
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define SKIP_TEST(name, reason) do { \
    printf("  %-50s \033[33mSKIP\033[0m (%s)\n", #name, reason); \
    neo__test_skip++; \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_TRUE(%s)\n", \
               __FILE__, __LINE__, #expr); \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_EQ(%s, %s) — got %lld, expected %lld\n", \
               __FILE__, __LINE__, #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_NEQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_NEQ(%s, %s) — both are %lld\n", \
               __FILE__, __LINE__, #a, #b, _a); \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_STR_EQ(%s, %s)\n    got: \"%s\"\n    exp: \"%s\"\n", \
               __FILE__, __LINE__, #a, #b, _a ? _a : "(null)", _b ? _b : "(null)"); \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_NOT_NULL(%s)\n", \
               __FILE__, __LINE__, #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        neo__test_fail++; neo__test_pass--; \
        printf("\033[31mFAIL\033[0m\n    %s:%d: ASSERT_NULL(%s)\n", \
               __FILE__, __LINE__, #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_GT(a, b) ASSERT_TRUE((a) > (b))
#define ASSERT_GTE(a, b) ASSERT_TRUE((a) >= (b))
#define ASSERT_LT(a, b) ASSERT_TRUE((a) < (b))
#define ASSERT_LTE(a, b) ASSERT_TRUE((a) <= (b))

#define TEST_MAIN_BEGIN(suite) \
    int main(void) { \
        printf("\n\033[1m[%s]\033[0m\n", suite);

#define TEST_MAIN_END \
        printf("\n  \033[1m%d passed\033[0m", neo__test_pass); \
        if (neo__test_fail) printf(", \033[31m%d failed\033[0m", neo__test_fail); \
        if (neo__test_skip) printf(", \033[33m%d skipped\033[0m", neo__test_skip); \
        printf("\n\n"); \
        return neo__test_fail > 0 ? 1 : 0; \
    }

#endif /* NEO_TEST_HARNESS_H */
