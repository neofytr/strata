# Test Runner

neobuild includes a built-in test runner for executing test binaries, collecting results, and reporting pass/fail status.

## Types

```c
typedef struct {
    int total;        // total number of tests
    int passed;       // tests that exited with code 0
    int failed;       // tests that exited with non-zero code
    int crashed;      // tests that were killed by a signal
    int timed_out;    // tests that exceeded the timeout
    double elapsed_ms; // total wall-clock time in milliseconds
} neo_test_results_t;
```

---

### `neo_test_suite_create`

> Create a new test suite.

```c
neo_test_suite_t *neo_test_suite_create(const char *name);
```

**Parameters:**
- `name` -- a human-readable name for the test suite (used in output).

**Returns:** Pointer to a new `neo_test_suite_t`, or `NULL` on failure.

**Example:**

```c
neo_test_suite_t *suite = neo_test_suite_create("unit-tests");
```

---

### `neo_test_suite_add`

> Add a test case to the suite. Each test is an executable command.

```c
void neo_test_suite_add(neo_test_suite_t *suite, const char *test_name,
                        const char *command);
```

**Parameters:**
- `suite` -- the test suite.
- `test_name` -- a name for this test case (used in output).
- `command` -- the command to run. Exit code 0 means pass; non-zero means fail.

**Returns:** Nothing.

**Example:**

```c
neo_test_suite_add(suite, "test_parser", "./build/test_parser");
neo_test_suite_add(suite, "test_lexer",  "./build/test_lexer");
neo_test_suite_add(suite, "test_arena",  "./build/test_arena");
```

---

### `neo_test_suite_set_timeout`

> Set the per-test timeout. Tests that exceed the timeout are killed and reported as timed out.

```c
void neo_test_suite_set_timeout(neo_test_suite_t *suite, int seconds);
```

**Parameters:**
- `suite` -- the test suite.
- `seconds` -- maximum number of seconds each test is allowed to run.

**Returns:** Nothing.

**Example:**

```c
neo_test_suite_set_timeout(suite, 30); // 30 seconds per test
```

---

### `neo_test_suite_run`

> Run all tests in the suite and return the results.

```c
neo_test_results_t neo_test_suite_run(neo_test_suite_t *suite);
```

**Parameters:**
- `suite` -- the test suite to run.

**Returns:** A `neo_test_results_t` struct containing counts and timing.

**Example:**

```c
neo_test_results_t r = neo_test_suite_run(suite);

printf("Results: %d/%d passed", r.passed, r.total);
if (r.failed > 0)    printf(", %d failed", r.failed);
if (r.crashed > 0)   printf(", %d crashed", r.crashed);
if (r.timed_out > 0) printf(", %d timed out", r.timed_out);
printf(" (%.1f ms)\n", r.elapsed_ms);
```

---

### `neo_test_suite_destroy`

> Destroy a test suite and free its memory.

```c
void neo_test_suite_destroy(neo_test_suite_t *suite);
```

**Parameters:**
- `suite` -- the test suite to destroy.

**Returns:** Nothing.

**Example:**

```c
neo_test_suite_destroy(suite);
```

---

## Complete Test Runner Example

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build");
    neo_set_profile(NEO_PROFILE_DEBUG);

    // Build test executables
    neo_graph_t *g = neo_graph_create();

    const char *t1_srcs[] = {"tests/test_parser.c"};
    neo_add_executable(g, "test_parser", t1_srcs, 1);

    const char *t2_srcs[] = {"tests/test_lexer.c"};
    neo_add_executable(g, "test_lexer", t2_srcs, 1);

    neo_graph_build(g);

    // Run tests
    neo_test_suite_t *suite = neo_test_suite_create("unit-tests");
    neo_test_suite_set_timeout(suite, 10);

    neo_test_suite_add(suite, "parser", "./build/test_parser");
    neo_test_suite_add(suite, "lexer",  "./build/test_lexer");

    neo_test_results_t r = neo_test_suite_run(suite);
    printf("%d/%d passed (%.1f ms)\n", r.passed, r.total, r.elapsed_ms);

    neo_test_suite_destroy(suite);
    neo_graph_destroy(g);

    return (r.failed + r.crashed + r.timed_out) > 0 ? 1 : 0;
}
```
