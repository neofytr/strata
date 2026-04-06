#include "neo_internal.h"

neo_test_suite_t *neo_test_suite_create(const char *name)
{
    neo_test_suite_t *s = (neo_test_suite_t *)calloc(1, sizeof(neo_test_suite_t));
    if (!s) return NULL;
    s->name = strdup(name ? name : "tests");
    s->cap = 16;
    s->tests = (neo_test_entry_t *)calloc(s->cap, sizeof(neo_test_entry_t));
    s->timeout_sec = 30;
    return s;
}

void neo_test_suite_add(neo_test_suite_t *suite, const char *name, const char *command)
{
    if (!suite || !name || !command) return;
    if (suite->count >= suite->cap) {
        suite->cap *= 2;
        suite->tests = (neo_test_entry_t *)realloc(suite->tests, suite->cap * sizeof(neo_test_entry_t));
    }
    suite->tests[suite->count].name = strdup(name);
    suite->tests[suite->count].command = strdup(command);
    suite->count++;
}

void neo_test_suite_set_timeout(neo_test_suite_t *suite, int seconds)
{
    if (suite) suite->timeout_sec = seconds;
}

neo_test_results_t neo_test_suite_run(neo_test_suite_t *suite)
{
    neo_test_results_t r = {0};
    if (!suite) return r;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    NEO_LOGF(NEO_LOG_INFO, "\n=== Running test suite: %s ===", suite->name);
    r.total = (int)suite->count;

    for (size_t i = 0; i < suite->count; i++) {
        neo_test_entry_t *t = &suite->tests[i];
        printf("  %-40s ", t->name);
        fflush(stdout);

        const char *argv[] = {"/bin/sh", "-c", t->command, NULL};
        neo_process_t proc = neo__spawn(argv);
        if (proc.pid <= 0) {
            printf("\033[31mFAIL\033[0m (spawn error)\n");
            r.failed++; continue;
        }

        int wstatus;
        /* simple timeout via alarm-style polling */
        struct timespec tstart;
        clock_gettime(CLOCK_MONOTONIC, &tstart);
        bool timed_out = false;

        while (1) {
            pid_t w = waitpid(proc.pid, &wstatus, WNOHANG);
            if (w > 0) break;
            if (w == -1) break;
            double elapsed = neo__elapsed_ms(&tstart);
            if (elapsed > suite->timeout_sec * 1000.0) {
                kill(proc.pid, SIGKILL);
                waitpid(proc.pid, &wstatus, 0);
                timed_out = true;
                break;
            }
            usleep(10000); /* 10ms poll */
        }

        if (timed_out) {
            printf("\033[33mTIMEOUT\033[0m\n");
            r.timed_out++;
        } else if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
            printf("\033[32mPASS\033[0m\n");
            r.passed++;
        } else if (WIFSIGNALED(wstatus)) {
            printf("\033[31mCRASH\033[0m (signal %d)\n", WTERMSIG(wstatus));
            r.crashed++;
        } else {
            printf("\033[31mFAIL\033[0m (exit %d)\n", WEXITSTATUS(wstatus));
            r.failed++;
        }
    }

    r.elapsed_ms = neo__elapsed_ms(&start);
    printf("\n  %d passed, %d failed, %d crashed, %d timed out (%.1fms)\n\n",
           r.passed, r.failed, r.crashed, r.timed_out, r.elapsed_ms);
    return r;
}

void neo_test_suite_destroy(neo_test_suite_t *suite)
{
    if (!suite) return;
    for (size_t i = 0; i < suite->count; i++) {
        free(suite->tests[i].name);
        free(suite->tests[i].command);
    }
    free(suite->tests);
    free(suite->name);
    free(suite);
}
