#include "neobuild.h"

#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <limits.h>
#include <libgen.h>
#include <signal.h>
#include <fcntl.h>

/* ════════════════════════════════════════════════════════════════
 *  Global State
 * ════════════════════════════════════════════════════════════════ */

static neocompiler_t g_default_compiler = NEO_GCC;
static neoverbosity_t g_verbosity       = NEO_NORMAL;
static neoprofile_t g_profile           = NEO_PROFILE_NONE;
static bool g_dry_run                   = false;
static int g_max_jobs                   = 1;
static char g_build_dir[PATH_MAX]       = {0};

/* compile_commands.json accumulator */
static vector_t g_compile_commands = NEOVEC_INIT;

typedef struct {
    char *directory;
    char *command;
    char *file;
} neo_cc_entry_t;

/* ════════════════════════════════════════════════════════════════
 *  neostr_t — Minimal String Builder (internal only)
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} neostr_t;

static neostr_t neostr_empty(void) { return (neostr_t){NULL, 0, 0}; }

static void neostr_free(neostr_t *s)
{
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

static bool neostr_ensure(neostr_t *s, size_t needed)
{
    if (s->cap >= needed) return true;
    size_t new_cap = s->cap ? s->cap : 16;
    while (new_cap < needed) new_cap *= 2;
    char *d = (char *)realloc(s->data, new_cap);
    if (!d) return false;
    s->data = d;
    s->cap = new_cap;
    return true;
}

static bool neostr_append(neostr_t *s, const char *cstr)
{
    if (!cstr) return true;
    size_t add = strlen(cstr);
    if (!neostr_ensure(s, s->len + add + 1)) return false;
    memcpy(s->data + s->len, cstr, add + 1);
    s->len += add;
    return true;
}

static bool neostr_appendf(neostr_t *s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static bool neostr_appendf(neostr_t *s, const char *fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) { va_end(args); return false; }
    if (!neostr_ensure(s, s->len + (size_t)needed + 1)) { va_end(args); return false; }
    vsnprintf(s->data + s->len, (size_t)needed + 1, fmt, args);
    s->len += (size_t)needed;
    va_end(args);
    return true;
}

static char *neostr_to_cstr(neostr_t *s)
{
    if (!s->data) return strdup("");
    return strdup(s->data);
}

static neostr_t neostr_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return neostr_empty();
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return neostr_empty(); }
    neostr_t s = neostr_empty();
    if (!neostr_ensure(&s, (size_t)size + 1)) { fclose(f); return neostr_empty(); }
    size_t rd = fread(s.data, 1, (size_t)size, f);
    fclose(f);
    s.data[rd] = '\0';
    s.len = rd;
    return s;
}

static char **neostr_split(const char *str, char delim, size_t *out_count)
{
    *out_count = 0;
    if (!str || !*str) return NULL;
    size_t cap = 8;
    char **arr = (char **)malloc(cap * sizeof(char *));
    if (!arr) return NULL;
    const char *start = str;
    while (1) {
        const char *end = strchr(start, delim);
        size_t len = end ? (size_t)(end - start) : strlen(start);
        if (*out_count >= cap) {
            cap *= 2;
            char **tmp = (char **)realloc(arr, cap * sizeof(char *));
            if (!tmp) { for (size_t i = 0; i < *out_count; i++) free(arr[i]); free(arr); *out_count = 0; return NULL; }
            arr = tmp;
        }
        arr[*out_count] = (char *)malloc(len + 1);
        if (!arr[*out_count]) { for (size_t i = 0; i < *out_count; i++) free(arr[i]); free(arr); *out_count = 0; return NULL; }
        memcpy(arr[*out_count], start, len);
        arr[*out_count][len] = '\0';
        (*out_count)++;
        if (!end) break;
        start = end + 1;
    }
    return arr;
}

static void neostr_free_split(char **arr, size_t count)
{
    if (!arr) return;
    for (size_t i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

/* ════════════════════════════════════════════════════════════════
 *  Arena Allocator
 * ════════════════════════════════════════════════════════════════ */

#define NEO_ARENA_DEFAULT_PAGE (64 * 1024)
#define NEO_ARENA_ALIGN 8

typedef struct neo_arena_page {
    struct neo_arena_page *next;
    size_t used;
    size_t capacity;
    char data[];
} neo_arena_page_t;

struct neo_arena {
    neo_arena_page_t *current;
    neo_arena_page_t *pages;
    size_t default_page_size;
};

static neo_arena_page_t *neo__arena_new_page(size_t min_size)
{
    size_t cap = min_size > NEO_ARENA_DEFAULT_PAGE ? min_size : NEO_ARENA_DEFAULT_PAGE;
    neo_arena_page_t *p = (neo_arena_page_t *)malloc(sizeof(neo_arena_page_t) + cap);
    if (!p) return NULL;
    p->next = NULL;
    p->used = 0;
    p->capacity = cap;
    return p;
}

neo_arena_t *neo_arena_create(size_t page_size)
{
    neo_arena_t *a = (neo_arena_t *)calloc(1, sizeof(neo_arena_t));
    if (!a) return NULL;
    a->default_page_size = page_size > 0 ? page_size : NEO_ARENA_DEFAULT_PAGE;
    a->current = neo__arena_new_page(a->default_page_size);
    if (!a->current) { free(a); return NULL; }
    a->pages = a->current;
    return a;
}

void *neo_arena_alloc(neo_arena_t *a, size_t size)
{
    if (!a || size == 0) return NULL;
    size_t aligned = (size + NEO_ARENA_ALIGN - 1) & ~(size_t)(NEO_ARENA_ALIGN - 1);

    if (a->current && a->current->used + aligned <= a->current->capacity) {
        void *ptr = a->current->data + a->current->used;
        a->current->used += aligned;
        return ptr;
    }

    /* need new page */
    size_t psize = aligned > a->default_page_size ? aligned : a->default_page_size;
    neo_arena_page_t *p = neo__arena_new_page(psize);
    if (!p) return NULL;
    p->next = a->pages;
    a->pages = p;
    a->current = p;
    void *ptr = p->data;
    p->used = aligned;
    return ptr;
}

char *neo_arena_strdup(neo_arena_t *a, const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *d = (char *)neo_arena_alloc(a, len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

char *neo_arena_sprintf(neo_arena_t *a, const char *fmt, ...)
{
    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) { va_end(args); return NULL; }
    char *buf = (char *)neo_arena_alloc(a, (size_t)needed + 1);
    if (buf) vsnprintf(buf, (size_t)needed + 1, fmt, args);
    va_end(args);
    return buf;
}

void neo_arena_destroy(neo_arena_t *a)
{
    if (!a) return;
    neo_arena_page_t *p = a->pages;
    while (p) {
        neo_arena_page_t *next = p->next;
        free(p);
        p = next;
    }
    free(a);
}

/* ════════════════════════════════════════════════════════════════
 *  Platform Abstraction Layer
 * ════════════════════════════════════════════════════════════════ */

typedef struct { pid_t pid; } neo_process_t;

static neo_process_t neo__spawn(const char *const argv[])
{
    neo_process_t proc = {-1};
    pid_t p = fork();
    if (p == -1) return proc;
    if (p == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    proc.pid = p;
    return proc;
}

static neo_process_t neo__spawn_shell(neoshell_t shell, const char *cmdline)
{
    const char *sh;
    switch (shell) {
    case NEO_BASH: sh = "/bin/bash"; break;
#ifndef NEO_PLATFORM_MACOS
    case NEO_DASH: sh = "/bin/dash"; break;
#endif
    default:       sh = "/bin/sh"; break;
    }
    const char *argv[] = {sh, "-c", cmdline, NULL};
    return neo__spawn(argv);
}

static int neo__wait(neo_process_t proc)
{
    if (proc.pid <= 0) return -1;
    int wstatus;
    if (waitpid(proc.pid, &wstatus, 0) == -1) return -1;
    if (WIFEXITED(wstatus)) return WEXITSTATUS(wstatus);
    return -1;
}

__attribute__((unused))
static neo_process_t neo__wait_any(int *exit_code)
{
    neo_process_t proc = {-1};
    int wstatus;
    pid_t p = waitpid(-1, &wstatus, 0);
    if (p <= 0) return proc;
    proc.pid = p;
    if (exit_code) {
        if (WIFEXITED(wstatus)) *exit_code = WEXITSTATUS(wstatus);
        else if (WIFSIGNALED(wstatus)) *exit_code = -WTERMSIG(wstatus);
        else *exit_code = -1;
    }
    return proc;
}

static char *neo__capture_output(const char *const argv[])
{
    int pipefd[2];
    if (pipe(pipefd) == -1) return NULL;

    pid_t p = fork();
    if (p == -1) { close(pipefd[0]); close(pipefd[1]); return NULL; }

    if (p == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);
    neostr_t buf = neostr_empty();
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp) - 1)) > 0) {
        tmp[n] = '\0';
        neostr_append(&buf, tmp);
    }
    close(pipefd[0]);

    int wstatus;
    waitpid(p, &wstatus, 0);

    /* trim trailing newline */
    if (buf.len > 0 && buf.data[buf.len - 1] == '\n')
        buf.data[--buf.len] = '\0';

    return neostr_to_cstr(&buf);
}

static const char *neo__ar_cmd(void)
{
    return "ar";
}

__attribute__((unused))
static const char *neo__ranlib_cmd(void)
{
    return "ranlib";
}

/* ════════════════════════════════════════════════════════════════
 *  Logging — NEO_LOGF / neo__logf
 * ════════════════════════════════════════════════════════════════ */

static const char *neo__level_str(neolog_level_t level)
{
    switch (level) {
    case NEO_LOG_ERROR:   return "ERROR";
    case NEO_LOG_WARNING: return "WARNING";
    case NEO_LOG_INFO:    return "INFO";
    case NEO_LOG_DEBUG:   return "DEBUG";
    default:              return "UNKNOWN";
    }
}

static const char *neo__level_color(neolog_level_t level)
{
    switch (level) {
    case NEO_LOG_ERROR:   return "\033[31m";
    case NEO_LOG_WARNING: return "\033[33m";
    case NEO_LOG_INFO:    return "\033[32m";
    case NEO_LOG_DEBUG:   return "\033[34m";
    default:              return "";
    }
}

void neo__logf(neolog_level_t level, const char *fmt, ...)
{
    if (g_verbosity == NEO_QUIET && level != NEO_LOG_ERROR) return;
    if (g_verbosity == NEO_NORMAL && level == NEO_LOG_DEBUG) return;

    FILE *out = (level <= NEO_LOG_WARNING) ? stderr : stdout;
    bool color = isatty(fileno(out));

    if (color) fprintf(out, "%s", neo__level_color(level));
    fprintf(out, "[%s] ", neo__level_str(level));
    if (color) fprintf(out, "\033[0m");

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);
    fprintf(out, "\n");
}

/* ════════════════════════════════════════════════════════════════
 *  Global Settings
 * ════════════════════════════════════════════════════════════════ */

void neo_set_global_default_compiler(neocompiler_t c) { g_default_compiler = c; }
neocompiler_t neo_get_global_default_compiler(void) { return g_default_compiler; }
void neo_set_verbosity(neoverbosity_t v) { g_verbosity = v; }
void neo_set_dry_run(bool enabled) { g_dry_run = enabled; }
void neo_set_profile(neoprofile_t p) { g_profile = p; }

void neo_set_build_dir(const char *dir)
{
    if (!dir) { g_build_dir[0] = '\0'; return; }
    snprintf(g_build_dir, sizeof(g_build_dir), "%s", dir);
    size_t len = strlen(g_build_dir);
    if (len > 0 && g_build_dir[len - 1] != '/' && len + 1 < sizeof(g_build_dir)) {
        g_build_dir[len] = '/';
        g_build_dir[len + 1] = '\0';
    }
}

void neo_set_jobs(int n)
{
    if (n <= 0) n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) n = 1;
    g_max_jobs = n;
}

/* ════════════════════════════════════════════════════════════════
 *  Compiler Helpers
 * ════════════════════════════════════════════════════════════════ */

static const char *neo__compiler_cmd(neocompiler_t c)
{
    switch (c) {
    case NEO_GCC:     return "gcc";
    case NEO_CLANG:   return "clang";
    case NEO_GPP:     return "g++";
    case NEO_CLANGPP: return "clang++";
    case NEO_LD:      return "ld";
    case NEO_AS:      return "as";
    default:          return "gcc";
    }
}

static neocompiler_t neo__resolve_compiler(neocompiler_t c)
{
    return (c == NEO_GLOBAL_DEFAULT) ? g_default_compiler : c;
}

static const char *neo__profile_flags(void)
{
    switch (g_profile) {
    case NEO_PROFILE_DEBUG:   return "-g -O0 -fsanitize=address -fsanitize=undefined";
    case NEO_PROFILE_RELEASE: return "-O3 -DNDEBUG -march=native";
    case NEO_PROFILE_RELDBG:  return "-O2 -g";
    default:                  return NULL;
    }
}

static double neo__elapsed_ms(struct timespec *start)
{
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start->tv_sec) * 1000.0 + (end.tv_nsec - start->tv_nsec) / 1e6;
}

/* ════════════════════════════════════════════════════════════════
 *  FNV-1a Hash (for content hashing)
 * ════════════════════════════════════════════════════════════════ */

static uint64_t neo__fnv1a(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t neo__fnv1a_str(const char *s)
{
    return s ? neo__fnv1a(s, strlen(s)) : 0;
}

static uint64_t neo__hash_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint64_t h = 14695981039346656037ULL;
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);
    return h;
}

/* ════════════════════════════════════════════════════════════════
 *  Command API
 * ════════════════════════════════════════════════════════════════ */

neocmd_t *neocmd_create(neoshell_t shell)
{
    neocmd_t *cmd = (neocmd_t *)malloc(sizeof(neocmd_t));
    if (!cmd) { NEO_LOGF(NEO_LOG_ERROR, "[neocmd_create] Allocation failed"); return NULL; }
    cmd->args = (vector_t)NEOVEC_INIT;
    cmd->shell = shell;
    return cmd;
}

bool neocmd_delete(neocmd_t *neocmd)
{
    if (!neocmd) return false;
    for (size_t i = 0; i < neocmd->args.count; i++)
        free(neocmd->args.items[i]);
    neovec_free(&neocmd->args);
    free(neocmd);
    return true;
}

bool neocmd_append_null(neocmd_t *neocmd, ...)
{
    if (!neocmd) return false;
    va_list args;
    va_start(args, neocmd);
    const char *arg = va_arg(args, const char *);
    while (arg) {
        char *dup = strdup(arg);
        if (!dup) { va_end(args); return false; }
        neovec_append(&neocmd->args, dup);
        arg = va_arg(args, const char *);
    }
    va_end(args);
    return true;
}

const char *neocmd_render(neocmd_t *neocmd)
{
    if (!neocmd) return NULL;
    neostr_t s = neostr_empty();
    for (size_t i = 0; i < neocmd->args.count; i++) {
        if (i > 0) neostr_append(&s, " ");
        neostr_append(&s, (const char *)neocmd->args.items[i]);
    }
    char *r = neostr_to_cstr(&s);
    neostr_free(&s);
    return r;
}

/* ════════════════════════════════════════════════════════════════
 *  Process Execution
 * ════════════════════════════════════════════════════════════════ */

bool neoshell_wait(pid_t pid, int *status, int *code, bool should_print)
{
    if (pid < 0) return false;
    siginfo_t info;
    if (waitid(P_PID, (id_t)pid, &info, WEXITED | WSTOPPED) == -1) {
        if (should_print) NEO_LOGF(NEO_LOG_ERROR, "[neoshell_wait] waitid failed: %s", strerror(errno));
        return false;
    }
    if (code) *code = info.si_code;
    if (status) *status = info.si_status;

    if (should_print) {
        static const struct { int code; const char *verb; neolog_level_t level; } tbl[] = {
            {CLD_EXITED,  "exited with status",           NEO_LOG_INFO},
            {CLD_KILLED,  "killed by signal",             NEO_LOG_ERROR},
            {CLD_DUMPED,  "killed by signal (core dump)", NEO_LOG_ERROR},
            {CLD_STOPPED, "stopped by signal",            NEO_LOG_ERROR},
            {CLD_TRAPPED, "trapped by signal",            NEO_LOG_ERROR},
        };
        bool found = false;
        for (size_t i = 0; i < sizeof(tbl)/sizeof(tbl[0]); i++) {
            if (info.si_code == tbl[i].code) {
                NEO_LOGF(tbl[i].level, "[neoshell_wait] pid %d %s %d", pid, tbl[i].verb, info.si_status);
                found = true; break;
            }
        }
        if (!found) NEO_LOGF(NEO_LOG_ERROR, "[neoshell_wait] pid %d unknown termination", pid);
    }
    return true;
}

pid_t neocmd_run_async(neocmd_t *neocmd)
{
    if (!neocmd) return -1;
    const char *command = neocmd_render(neocmd);
    if (!command) return -1;

    NEO_LOGF(NEO_LOG_DEBUG, "[run] %s", command);

    if (g_dry_run) {
        NEO_LOGF(NEO_LOG_INFO, "[DRY-RUN] %s", command);
        free((void *)command);
        return 0;
    }

    neo_process_t proc;
    if (neocmd->shell == NEO_DIRECT) {
        /* build argv from the vector directly */
        const char **argv = (const char **)malloc((neocmd->args.count + 1) * sizeof(char *));
        if (!argv) { free((void *)command); return -1; }
        for (size_t i = 0; i < neocmd->args.count; i++)
            argv[i] = (const char *)neocmd->args.items[i];
        argv[neocmd->args.count] = NULL;
        proc = neo__spawn(argv);
        free(argv);
    } else {
        proc = neo__spawn_shell(neocmd->shell, command);
    }

    free((void *)command);
    return proc.pid;
}

bool neocmd_run_sync(neocmd_t *neocmd, int *status, int *code, bool print_status_desc)
{
    if (g_dry_run) {
        const char *command = neocmd_render(neocmd);
        if (command) { NEO_LOGF(NEO_LOG_INFO, "[DRY-RUN] %s", command); free((void *)command); }
        if (status) *status = 0;
        if (code) *code = CLD_EXITED;
        return true;
    }
    pid_t child = neocmd_run_async(neocmd);
    if (child == -1) return false;
    neoshell_wait(child, status, code, print_status_desc);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Header Dependency Tracking
 * ════════════════════════════════════════════════════════════════ */

#define NEO_MAX_DEPS 256

static bool neo__scan_includes(const char *filepath, time_t output_mtime,
                               const char **visited, size_t *visited_count, bool *newer)
{
    if (*newer) return true;
    char real[PATH_MAX];
    if (!realpath(filepath, real)) return false;
    for (size_t i = 0; i < *visited_count; i++)
        if (strcmp(visited[i], real) == 0) return true;
    if (*visited_count >= NEO_MAX_DEPS) return true;
    visited[*visited_count] = strdup(real);
    (*visited_count)++;

    struct stat st;
    if (stat(real, &st) == -1) return false;
    if (st.st_mtime > output_mtime) { *newer = true; return true; }

    FILE *f = fopen(real, "r");
    if (!f) return false;
    char dir[PATH_MAX];
    strncpy(dir, real, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *slash = strrchr(dir, '/');
    if (slash) *(slash + 1) = '\0';

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p != '#') continue;
        p++;
        while (*p && isspace(*p)) p++;
        if (strncmp(p, "include", 7) != 0) continue;
        p += 7;
        while (*p && isspace(*p)) p++;
        if (*p != '"') continue;
        p++;
        const char *end = strchr(p, '"');
        if (!end) continue;
        size_t inc_len = (size_t)(end - p);
        size_t dir_len = strlen(dir);
        if (dir_len + inc_len >= PATH_MAX) continue;
        char inc_path[PATH_MAX];
        memcpy(inc_path, dir, dir_len);
        memcpy(inc_path + dir_len, p, inc_len);
        inc_path[dir_len + inc_len] = '\0';
        neo__scan_includes(inc_path, output_mtime, visited, visited_count, newer);
        if (*newer) break;
    }
    fclose(f);
    return true;
}

bool neo_needs_recompile(const char *source, const char *output)
{
    if (!source || !output) return true;
    struct stat output_stat;
    if (stat(output, &output_stat) == -1) return true;
    const char *visited[NEO_MAX_DEPS] = {0};
    size_t visited_count = 0;
    bool newer = false;
    neo__scan_includes(source, output_stat.st_mtime, visited, &visited_count, &newer);
    for (size_t i = 0; i < visited_count; i++) free((void *)visited[i]);
    return newer;
}

/* ════════════════════════════════════════════════════════════════
 *  Content Hash Database
 * ════════════════════════════════════════════════════════════════ */

#define NEO_BUILDDB_MAGIC 0x4E454F4442ULL  /* "NEODB" */
#define NEO_BUILDDB_VERSION 1
#define NEO_BUILDDB_MAX_PATH 512

typedef struct {
    char source_path[NEO_BUILDDB_MAX_PATH];
    uint64_t source_hash;
    uint64_t flags_hash;
    uint64_t compiler_hash;
} neo_builddb_entry_t;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t count;
} neo_builddb_header_t;

typedef struct {
    neo_builddb_entry_t *entries;
    size_t count;
    size_t cap;
    bool dirty;
    char path[PATH_MAX];
} neo_builddb_t;

__attribute__((unused))
static neo_builddb_t *neo__builddb_load(const char *path)
{
    neo_builddb_t *db = (neo_builddb_t *)calloc(1, sizeof(neo_builddb_t));
    if (!db) return NULL;
    snprintf(db->path, sizeof(db->path), "%s", path);

    FILE *f = fopen(path, "rb");
    if (f) {
        neo_builddb_header_t hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) == 1 &&
            hdr.magic == NEO_BUILDDB_MAGIC && hdr.version == NEO_BUILDDB_VERSION) {
            db->count = hdr.count;
            db->cap = hdr.count + 16;
            db->entries = (neo_builddb_entry_t *)malloc(db->cap * sizeof(neo_builddb_entry_t));
            if (db->entries)
                fread(db->entries, sizeof(neo_builddb_entry_t), hdr.count, f);
        }
        fclose(f);
    }
    if (!db->entries) {
        db->cap = 64;
        db->entries = (neo_builddb_entry_t *)calloc(db->cap, sizeof(neo_builddb_entry_t));
    }
    return db;
}

__attribute__((unused))
static bool neo__builddb_save(neo_builddb_t *db)
{
    if (!db || !db->dirty) return true;
    size_t plen = strlen(db->path);
    char *tmp = (char *)malloc(plen + 5);
    if (!tmp) return false;
    memcpy(tmp, db->path, plen);
    memcpy(tmp + plen, ".tmp", 5);
    FILE *f = fopen(tmp, "wb");
    if (!f) { free(tmp); return false; }
    neo_builddb_header_t hdr = {NEO_BUILDDB_MAGIC, NEO_BUILDDB_VERSION, (uint32_t)db->count};
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(db->entries, sizeof(neo_builddb_entry_t), db->count, f);
    fclose(f);
    bool ok = rename(tmp, db->path) == 0;
    free(tmp);
    return ok;
}

__attribute__((unused))
static bool neo__builddb_needs_rebuild(neo_builddb_t *db, const char *source,
                                       const char *flags, const char *compiler)
{
    uint64_t src_hash = neo__hash_file(source);
    uint64_t flags_hash = neo__fnv1a_str(flags);
    uint64_t comp_hash = neo__fnv1a_str(compiler);

    for (size_t i = 0; i < db->count; i++) {
        if (strcmp(db->entries[i].source_path, source) == 0) {
            return db->entries[i].source_hash != src_hash ||
                   db->entries[i].flags_hash != flags_hash ||
                   db->entries[i].compiler_hash != comp_hash;
        }
    }
    return true; /* not in database */
}

__attribute__((unused))
static void neo__builddb_update(neo_builddb_t *db, const char *source,
                                 const char *flags, const char *compiler)
{
    uint64_t src_hash = neo__hash_file(source);
    uint64_t flags_hash = neo__fnv1a_str(flags);
    uint64_t comp_hash = neo__fnv1a_str(compiler);

    for (size_t i = 0; i < db->count; i++) {
        if (strcmp(db->entries[i].source_path, source) == 0) {
            db->entries[i].source_hash = src_hash;
            db->entries[i].flags_hash = flags_hash;
            db->entries[i].compiler_hash = comp_hash;
            db->dirty = true;
            return;
        }
    }
    if (db->count >= db->cap) {
        db->cap *= 2;
        db->entries = (neo_builddb_entry_t *)realloc(db->entries, db->cap * sizeof(neo_builddb_entry_t));
    }
    neo_builddb_entry_t *e = &db->entries[db->count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->source_path, sizeof(e->source_path), "%s", source);
    e->source_hash = src_hash;
    e->flags_hash = flags_hash;
    e->compiler_hash = comp_hash;
    db->dirty = true;
}

__attribute__((unused))
static void neo__builddb_free(neo_builddb_t *db)
{
    if (!db) return;
    free(db->entries);
    free(db);
}

/* ════════════════════════════════════════════════════════════════
 *  compile_commands.json
 * ════════════════════════════════════════════════════════════════ */

static void neo__record_compile_command(const char *source, const char *cmd_str)
{
    neo_cc_entry_t *entry = (neo_cc_entry_t *)malloc(sizeof(neo_cc_entry_t));
    if (!entry) return;
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
    entry->directory = strdup(cwd);
    entry->command = strdup(cmd_str);
    entry->file = strdup(source);
    neovec_append(&g_compile_commands, entry);
}

static void neo__json_escape(neostr_t *s, const char *str)
{
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':  neostr_append(s, "\\\""); break;
        case '\\': neostr_append(s, "\\\\"); break;
        case '\n': neostr_append(s, "\\n"); break;
        case '\t': neostr_append(s, "\\t"); break;
        default:   neostr_appendf(s, "%c", *p); break;
        }
    }
}

bool neo_export_compile_commands(const char *output_path)
{
    if (!output_path) return false;
    neostr_t json = neostr_empty();
    neostr_append(&json, "[\n");
    for (size_t i = 0; i < g_compile_commands.count; i++) {
        neo_cc_entry_t *e = (neo_cc_entry_t *)g_compile_commands.items[i];
        if (i > 0) neostr_append(&json, ",\n");
        neostr_append(&json, "  {\n    \"directory\": \"");
        neo__json_escape(&json, e->directory);
        neostr_append(&json, "\",\n    \"command\": \"");
        neo__json_escape(&json, e->command);
        neostr_append(&json, "\",\n    \"file\": \"");
        neo__json_escape(&json, e->file);
        neostr_append(&json, "\"\n  }");
    }
    neostr_append(&json, "\n]\n");
    FILE *f = fopen(output_path, "w");
    if (!f) { neostr_free(&json); return false; }
    fputs(json.data ? json.data : "[]", f);
    fclose(f);
    neostr_free(&json);
    NEO_LOGF(NEO_LOG_INFO, "Wrote compile_commands.json (%zu entries)", g_compile_commands.count);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Output Path Helper
 * ════════════════════════════════════════════════════════════════ */

static char *neo__make_output_path(const char *source, const char *output)
{
    if (output) return strdup(output);
    const char *base = strrchr(source, '/');
    base = base ? base + 1 : source;
    const char *ext = strrchr(base, '.');
    neostr_t path = neostr_empty();
    if (g_build_dir[0]) neostr_append(&path, g_build_dir);
    else if (base != source) neostr_appendf(&path, "%.*s", (int)(base - source), source);
    if (ext) neostr_appendf(&path, "%.*s.o", (int)(ext - base), base);
    else neostr_appendf(&path, "%s.o", base);
    char *result = neostr_to_cstr(&path);
    neostr_free(&path);
    return result;
}

/* ════════════════════════════════════════════════════════════════
 *  Compilation
 * ════════════════════════════════════════════════════════════════ */

bool neo_compile_to_object_file(neocompiler_t compiler, const char *source,
    const char *output, const char *compiler_flags, bool force_compilation)
{
    if (!source) { NEO_LOGF(NEO_LOG_ERROR, "Source path cannot be NULL"); return false; }

    char *output_name = neo__make_output_path(source, output);
    if (!output_name) return false;
    if (g_build_dir[0]) neo_mkdir(g_build_dir, 0755);

    struct stat source_stat;
    if (stat(source, &source_stat) == -1) {
        NEO_LOGF(NEO_LOG_ERROR, "Cannot access '%s': %s", source, strerror(errno));
        free(output_name); return false;
    }

    if (!force_compilation && !neo_needs_recompile(source, output_name)) {
        NEO_LOGF(NEO_LOG_DEBUG, "'%s' is up to date", output_name);
        free(output_name); return true;
    }

    compiler = neo__resolve_compiler(compiler);
    const char *cc = neo__compiler_cmd(compiler);
    const char *pf = neo__profile_flags();

    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) { free(output_name); return false; }

    neocmd_append(cmd, cc, "-c", source, "-o", output_name);
    if (pf) neocmd_append(cmd, pf);
    if (compiler_flags) neocmd_append(cmd, compiler_flags);

    const char *rendered = neocmd_render(cmd);
    if (rendered) { neo__record_compile_command(source, rendered); free((void *)rendered); }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int status, code;
    bool result = neocmd_run_sync(cmd, &status, &code, false);
    double elapsed = neo__elapsed_ms(&start);

    if (!result) NEO_LOGF(NEO_LOG_ERROR, "Shell execution failed for '%s'", source);
    else if (code != CLD_EXITED || status != 0) { NEO_LOGF(NEO_LOG_ERROR, "Compilation failed: '%s'", source); result = false; }
    else NEO_LOGF(NEO_LOG_INFO, "Compiled '%s' in %.1fms", source, elapsed);

    neocmd_delete(cmd);
    free(output_name);
    return result;
}

int neo_compile_parallel(neocompiler_t compiler, const char **sources,
    size_t count, const char *compiler_flags, bool force_compilation)
{
    if (!sources || count == 0) return -1;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    compiler = neo__resolve_compiler(compiler);
    const char *cc = neo__compiler_cmd(compiler);
    const char *pf = neo__profile_flags();
    if (g_build_dir[0]) neo_mkdir(g_build_dir, 0755);

    typedef struct { size_t idx; char *output; } job_t;
    job_t *jobs = (job_t *)malloc(count * sizeof(job_t));
    if (!jobs) return -1;
    size_t job_count = 0;

    for (size_t i = 0; i < count; i++) {
        char *out = neo__make_output_path(sources[i], NULL);
        if (!out) continue;
        if (!force_compilation && !neo_needs_recompile(sources[i], out)) { free(out); continue; }
        jobs[job_count].idx = i;
        jobs[job_count].output = out;
        job_count++;
    }

    if (job_count == 0) {
        NEO_LOGF(NEO_LOG_INFO, "All %zu sources up to date", count);
        free(jobs); return (int)count;
    }

    NEO_LOGF(NEO_LOG_INFO, "Compiling %zu/%zu with %d jobs", job_count, count, g_max_jobs);

    typedef struct { pid_t pid; size_t job_idx; } slot_t;
    slot_t *slots = (slot_t *)calloc((size_t)g_max_jobs, sizeof(slot_t));
    if (!slots) { free(jobs); return -1; }
    int active = 0, successes = (int)(count - job_count);
    size_t next = 0;

    while (next < job_count || active > 0) {
        while (active < g_max_jobs && next < job_count) {
            job_t *j = &jobs[next];
            neostr_t cs = neostr_empty();
            neostr_appendf(&cs, "%s -c %s -o %s", cc, sources[j->idx], j->output);
            if (pf) neostr_appendf(&cs, " %s", pf);
            if (compiler_flags) neostr_appendf(&cs, " %s", compiler_flags);
            if (cs.data) neo__record_compile_command(sources[j->idx], cs.data);

            if (g_dry_run) {
                NEO_LOGF(NEO_LOG_INFO, "[DRY-RUN] %s", cs.data ? cs.data : "");
                neostr_free(&cs); successes++; next++; continue;
            }

            const char *argv[] = {"/bin/sh", "-c", cs.data, NULL};
            neo_process_t proc = neo__spawn(argv);
            neostr_free(&cs);
            if (proc.pid <= 0) { next++; continue; }
            slots[active].pid = proc.pid;
            slots[active].job_idx = next;
            active++; next++;
        }
        if (active == 0) break;
        int wstatus;
        pid_t fin = waitpid(-1, &wstatus, 0);
        if (fin <= 0) continue;
        for (int s = 0; s < active; s++) {
            if (slots[s].pid == fin) {
                if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0) {
                    NEO_LOGF(NEO_LOG_INFO, "Compiled '%s'", sources[jobs[slots[s].job_idx].idx]);
                    successes++;
                } else {
                    NEO_LOGF(NEO_LOG_ERROR, "Failed '%s'", sources[jobs[slots[s].job_idx].idx]);
                }
                slots[s] = slots[active - 1]; active--; break;
            }
        }
    }

    double elapsed = neo__elapsed_ms(&start);
    NEO_LOGF(NEO_LOG_INFO, "Parallel compile: %d/%zu in %.1fms", successes, count, elapsed);
    for (size_t i = 0; i < job_count; i++) free(jobs[i].output);
    free(jobs); free(slots);
    return successes;
}

/* ════════════════════════════════════════════════════════════════
 *  Library Building
 * ════════════════════════════════════════════════════════════════ */

bool neo_build_static_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags)
{
    if (!name || !sources || nsources == 0) return false;
    int compiled = neo_compile_parallel(compiler, sources, nsources, cflags, false);
    if (compiled < 0 || (size_t)compiled < nsources) return false;

    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) return false;
    neocmd_append(cmd, neo__ar_cmd(), "rcs", name);
    for (size_t i = 0; i < nsources; i++) {
        char *obj = neo__make_output_path(sources[i], NULL);
        if (obj) { neocmd_append(cmd, obj); free(obj); }
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status, code;
    bool result = neocmd_run_sync(cmd, &status, &code, false);
    double elapsed = neo__elapsed_ms(&start);

    if (result && code == CLD_EXITED && status == 0)
        NEO_LOGF(NEO_LOG_INFO, "Built static lib '%s' in %.1fms", name, elapsed);
    else { NEO_LOGF(NEO_LOG_ERROR, "Failed to build '%s'", name); result = false; }

    neocmd_delete(cmd);
    return result;
}

bool neo_build_shared_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags,
    const char *ldflags)
{
    if (!name || !sources || nsources == 0) return false;

    /* compile with -fPIC */
    neostr_t flags = neostr_empty();
    neostr_append(&flags, "-fPIC");
    if (cflags) neostr_appendf(&flags, " %s", cflags);
    int compiled = neo_compile_parallel(compiler, sources, nsources, flags.data, false);
    neostr_free(&flags);
    if (compiled < 0 || (size_t)compiled < nsources) return false;

    compiler = neo__resolve_compiler(compiler);
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) return false;

#ifdef NEO_PLATFORM_MACOS
    neocmd_append(cmd, neo__compiler_cmd(compiler), "-dynamiclib", "-o", name);
#else
    neocmd_append(cmd, neo__compiler_cmd(compiler), "-shared", "-o", name);
#endif

    for (size_t i = 0; i < nsources; i++) {
        char *obj = neo__make_output_path(sources[i], NULL);
        if (obj) { neocmd_append(cmd, obj); free(obj); }
    }
    if (ldflags) neocmd_append(cmd, ldflags);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status, code;
    bool result = neocmd_run_sync(cmd, &status, &code, false);
    double elapsed = neo__elapsed_ms(&start);

    if (result && code == CLD_EXITED && status == 0)
        NEO_LOGF(NEO_LOG_INFO, "Built shared lib '%s' in %.1fms", name, elapsed);
    else { NEO_LOGF(NEO_LOG_ERROR, "Failed to build '%s'", name); result = false; }

    neocmd_delete(cmd);
    return result;
}

/* ════════════════════════════════════════════════════════════════
 *  Linking
 * ════════════════════════════════════════════════════════════════ */

bool neo_link_null(neocompiler_t compiler, const char *executable,
    const char *linker_flags, bool forced_linking, ...)
{
    if (!executable) { NEO_LOGF(NEO_LOG_ERROR, "No executable name"); return false; }

    struct { const char **items; size_t count; size_t capacity; } objects = NEOVEC_INIT;
    va_list args;
    va_start(args, forced_linking);
    const char *tmp = va_arg(args, const char *);
    while (tmp) { neovec_append(&objects, tmp); tmp = va_arg(args, const char *); }
    va_end(args);
    if (!objects.count) { neovec_free(&objects); return false; }

    bool requires = forced_linking;
    if (!forced_linking) {
        struct stat exec_stat;
        bool exists = (stat(executable, &exec_stat) == 0);
        if (!exists) { requires = true; }
        else {
            for (size_t i = 0; i < objects.count; i++) {
                struct stat os;
                if (stat((const char *)objects.items[i], &os) == -1) {
                    NEO_LOGF(NEO_LOG_ERROR, "Cannot access '%s'", (const char *)objects.items[i]);
                    neovec_free(&objects); return false;
                }
                if (os.st_mtime > exec_stat.st_mtime) requires = true;
            }
        }
        if (!requires) {
            NEO_LOGF(NEO_LOG_DEBUG, "'%s' up to date", executable);
            neovec_free(&objects); return true;
        }
    }

    compiler = neo__resolve_compiler(compiler);
    neocmd_t *cmd = neocmd_create(NEO_SH);
    if (!cmd) { neovec_free(&objects); return false; }
    neocmd_append(cmd, neo__compiler_cmd(compiler), "-o", executable);
    for (size_t i = 0; i < objects.count; i++)
        neocmd_append(cmd, (const char *)objects.items[i]);
    if (linker_flags) neocmd_append(cmd, linker_flags);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status, code;
    bool result = neocmd_run_sync(cmd, &status, &code, false);
    double elapsed = neo__elapsed_ms(&start);

    if (!result || code != CLD_EXITED || status != 0)
    { NEO_LOGF(NEO_LOG_ERROR, "Linking failed: '%s'", executable); result = false; }
    else NEO_LOGF(NEO_LOG_INFO, "Linked '%s' in %.1fms", executable, elapsed);

    neocmd_delete(cmd); neovec_free(&objects);
    return result;
}

/* ════════════════════════════════════════════════════════════════
 *  Toolchain
 * ════════════════════════════════════════════════════════════════ */

struct neo_toolchain {
    char *prefix;
    char *sysroot;
    char *cc;
    char *cxx;
    char *ar;
    char *ranlib;
};

neo_toolchain_t *neo_toolchain_create(const char *prefix)
{
    neo_toolchain_t *tc = (neo_toolchain_t *)calloc(1, sizeof(neo_toolchain_t));
    if (!tc) return NULL;
    if (prefix) tc->prefix = strdup(prefix);
    return tc;
}

void neo_toolchain_set_sysroot(neo_toolchain_t *tc, const char *sysroot)
{
    if (!tc) return;
    free(tc->sysroot);
    tc->sysroot = sysroot ? strdup(sysroot) : NULL;
}

void neo_toolchain_set_cc(neo_toolchain_t *tc, const char *cc)
{
    if (!tc) return;
    free(tc->cc);
    tc->cc = cc ? strdup(cc) : NULL;
}

void neo_toolchain_set_cxx(neo_toolchain_t *tc, const char *cxx)
{
    if (!tc) return;
    free(tc->cxx);
    tc->cxx = cxx ? strdup(cxx) : NULL;
}

void neo_toolchain_destroy(neo_toolchain_t *tc)
{
    if (!tc) return;
    free(tc->prefix); free(tc->sysroot);
    free(tc->cc); free(tc->cxx);
    free(tc->ar); free(tc->ranlib);
    free(tc);
}

/* ════════════════════════════════════════════════════════════════
 *  Target Graph
 * ════════════════════════════════════════════════════════════════ */

typedef enum {
    NEO_TSTATE_PENDING,
    NEO_TSTATE_READY,
    NEO_TSTATE_BUILDING,
    NEO_TSTATE_DONE,
    NEO_TSTATE_FAILED,
} neo_target_state_t;

struct neo_target {
    uint32_t id;
    char *name;
    neo_target_type_t type;
    neocompiler_t compiler;
    char **sources;
    size_t nsources;
    char *cflags;
    char *ldflags;
    char *include_dirs;
    char *custom_command;
    uint32_t *deps;
    size_t ndeps;
    size_t deps_cap;
    int version_major, version_minor, version_patch;
    bool has_version;
    neo_target_state_t state;
    pid_t build_pid;
    neo_toolchain_t *toolchain;
    bool use_content_hash;
    bool use_ccache;
};

struct neo_graph {
    neo_target_t **targets;
    size_t ntargets;
    size_t targets_cap;
    neo_arena_t *arena;
    neo_toolchain_t *toolchain;
    bool content_hash;
    bool ccache;
};

neo_graph_t *neo_graph_create(void)
{
    neo_graph_t *g = (neo_graph_t *)calloc(1, sizeof(neo_graph_t));
    if (!g) return NULL;
    g->arena = neo_arena_create(0);
    if (!g->arena) { free(g); return NULL; }
    g->targets_cap = 16;
    g->targets = (neo_target_t **)calloc(g->targets_cap, sizeof(neo_target_t *));
    if (!g->targets) { neo_arena_destroy(g->arena); free(g); return NULL; }
    return g;
}

void neo_graph_destroy(neo_graph_t *g)
{
    if (!g) return;
    /* targets are arena-allocated except deps which we manually free */
    for (size_t i = 0; i < g->ntargets; i++)
        free(g->targets[i]->deps);
    free(g->targets);
    neo_arena_destroy(g->arena);
    free(g);
}

static neo_target_t *neo__graph_add(neo_graph_t *g, const char *name,
    neo_target_type_t type, const char **sources, size_t nsources)
{
    if (g->ntargets >= g->targets_cap) {
        g->targets_cap *= 2;
        g->targets = (neo_target_t **)realloc(g->targets, g->targets_cap * sizeof(neo_target_t *));
    }
    neo_target_t *t = (neo_target_t *)neo_arena_alloc(g->arena, sizeof(neo_target_t));
    memset(t, 0, sizeof(*t));
    t->id = (uint32_t)g->ntargets;
    t->name = neo_arena_strdup(g->arena, name);
    t->type = type;
    t->compiler = NEO_GLOBAL_DEFAULT;
    if (sources && nsources > 0) {
        t->sources = (char **)neo_arena_alloc(g->arena, nsources * sizeof(char *));
        for (size_t i = 0; i < nsources; i++)
            t->sources[i] = neo_arena_strdup(g->arena, sources[i]);
        t->nsources = nsources;
    }
    t->deps_cap = 4;
    t->deps = (uint32_t *)calloc(t->deps_cap, sizeof(uint32_t));
    g->targets[g->ntargets++] = t;
    return t;
}

neo_target_t *neo_add_executable(neo_graph_t *g, const char *name,
    const char **sources, size_t nsources)
{
    return neo__graph_add(g, name, NEO_TARGET_EXECUTABLE, sources, nsources);
}

neo_target_t *neo_add_static_lib(neo_graph_t *g, const char *name,
    const char **sources, size_t nsources)
{
    return neo__graph_add(g, name, NEO_TARGET_STATIC_LIB, sources, nsources);
}

neo_target_t *neo_add_shared_lib(neo_graph_t *g, const char *name,
    const char **sources, size_t nsources)
{
    return neo__graph_add(g, name, NEO_TARGET_SHARED_LIB, sources, nsources);
}

neo_target_t *neo_add_custom(neo_graph_t *g, const char *name, const char *command)
{
    neo_target_t *t = neo__graph_add(g, name, NEO_TARGET_CUSTOM, NULL, 0);
    if (t) t->custom_command = neo_arena_strdup(g->arena, command);
    return t;
}

void neo_target_set_compiler(neo_target_t *t, neocompiler_t c) { if (t) t->compiler = c; }

void neo_target_add_cflags(neo_target_t *t, const char *flags)
{
    if (!t || !flags) return;
    if (t->cflags) {
        size_t old_len = strlen(t->cflags);
        size_t add_len = strlen(flags);
        char *buf = (char *)malloc(old_len + 1 + add_len + 1);
        if (!buf) return;
        sprintf(buf, "%s %s", t->cflags, flags);
        free(t->cflags);
        t->cflags = buf;
    } else {
        t->cflags = strdup(flags);
    }
}

void neo_target_add_ldflags(neo_target_t *t, const char *flags)
{
    if (!t || !flags) return;
    if (t->ldflags) {
        char *buf = (char *)malloc(strlen(t->ldflags) + 1 + strlen(flags) + 1);
        if (!buf) return;
        sprintf(buf, "%s %s", t->ldflags, flags);
        free(t->ldflags);
        t->ldflags = buf;
    } else {
        t->ldflags = strdup(flags);
    }
}

void neo_target_add_include_dir(neo_target_t *t, const char *dir)
{
    if (!t || !dir) return;
    char flag[PATH_MAX + 4];
    snprintf(flag, sizeof(flag), "-I%s", dir);
    neo_target_add_cflags(t, flag);
}

void neo_target_depends_on(neo_target_t *target, neo_target_t *dep)
{
    if (!target || !dep) return;
    if (target->ndeps >= target->deps_cap) {
        target->deps_cap *= 2;
        target->deps = (uint32_t *)realloc(target->deps, target->deps_cap * sizeof(uint32_t));
    }
    target->deps[target->ndeps++] = dep->id;
}

void neo_target_set_version(neo_target_t *t, int major, int minor, int patch)
{
    if (!t) return;
    t->version_major = major;
    t->version_minor = minor;
    t->version_patch = patch;
    t->has_version = true;
}

void neo_target_set_toolchain(neo_target_t *t, neo_toolchain_t *tc)
{
    if (t) t->toolchain = tc;
}

neo_target_t *neo_graph_find(neo_graph_t *g, const char *name)
{
    if (!g || !name) return NULL;
    for (size_t i = 0; i < g->ntargets; i++)
        if (strcmp(g->targets[i]->name, name) == 0)
            return g->targets[i];
    return NULL;
}

void neo_graph_set_toolchain(neo_graph_t *g, neo_toolchain_t *tc)
{
    if (g) g->toolchain = tc;
}

void neo_graph_enable_content_hash(neo_graph_t *g) { if (g) g->content_hash = true; }
void neo_graph_enable_ccache(neo_graph_t *g) { if (g) g->ccache = true; }

/* Build a single target (internal) */
static bool neo__build_single_target(neo_graph_t *g, neo_target_t *t)
{
    neocompiler_t cc = (t->compiler != NEO_GLOBAL_DEFAULT) ? t->compiler : g_default_compiler;
    const char *cc_str = neo__compiler_cmd(cc);
    NEO_LOGF(NEO_LOG_INFO, "Building target '%s' (%s)", t->name,
             t->type == NEO_TARGET_EXECUTABLE ? "executable" :
             t->type == NEO_TARGET_STATIC_LIB ? "static lib" :
             t->type == NEO_TARGET_SHARED_LIB ? "shared lib" : "custom");

    /* toolchain prefix */
    char tc_cc[512] = {0};
    neo_toolchain_t *tc = t->toolchain ? t->toolchain : g->toolchain;
    if (tc) {
        if (tc->cc) snprintf(tc_cc, sizeof(tc_cc), "%s", tc->cc);
        else if (tc->prefix) snprintf(tc_cc, sizeof(tc_cc), "%s%s", tc->prefix, cc_str);
    }
    const char *effective_cc = tc_cc[0] ? tc_cc : cc_str;

    /* build sysroot flag */
    char sysroot_flag[PATH_MAX + 16] = {0};
    if (tc && tc->sysroot) snprintf(sysroot_flag, sizeof(sysroot_flag), "--sysroot=%s", tc->sysroot);

    /* ccache prefix */
    char *ccache_prefix = NULL;
    if (g->ccache) {
        const char *ccache_argv[] = {"which", "ccache", NULL};
        ccache_prefix = neo__capture_output(ccache_argv);
        if (!ccache_prefix || strlen(ccache_prefix) == 0) {
            const char *sccache_argv[] = {"which", "sccache", NULL};
            free(ccache_prefix);
            ccache_prefix = neo__capture_output(sccache_argv);
        }
    }

    if (t->type == NEO_TARGET_CUSTOM) {
        if (!t->custom_command) return false;
        neocmd_t *cmd = neocmd_create(NEO_SH);
        neocmd_append(cmd, t->custom_command);
        int status, code;
        bool ok = neocmd_run_sync(cmd, &status, &code, false);
        neocmd_delete(cmd);
        free(ccache_prefix);
        return ok && code == CLD_EXITED && status == 0;
    }

    if (!t->sources || t->nsources == 0) { free(ccache_prefix); return false; }

    /* build cflags */
    neostr_t full_cflags = neostr_empty();
    if (t->type == NEO_TARGET_SHARED_LIB) neostr_append(&full_cflags, "-fPIC ");
    if (sysroot_flag[0]) neostr_appendf(&full_cflags, "%s ", sysroot_flag);
    const char *pf = neo__profile_flags();
    if (pf) neostr_appendf(&full_cflags, "%s ", pf);
    if (t->cflags) neostr_appendf(&full_cflags, "%s ", t->cflags);

    /* compile sources */
    for (size_t i = 0; i < t->nsources; i++) {
        char *obj = neo__make_output_path(t->sources[i], NULL);
        if (!obj) continue;

        if (!neo_needs_recompile(t->sources[i], obj)) {
            NEO_LOGF(NEO_LOG_DEBUG, "'%s' up to date", obj);
            free(obj); continue;
        }

        neocmd_t *cmd = neocmd_create(NEO_SH);
        if (ccache_prefix && strlen(ccache_prefix) > 0)
            neocmd_append(cmd, ccache_prefix);
        neocmd_append(cmd, effective_cc, "-c", t->sources[i], "-o", obj);
        if (full_cflags.data && full_cflags.len > 0) neocmd_append(cmd, full_cflags.data);

        const char *rendered = neocmd_render(cmd);
        if (rendered) { neo__record_compile_command(t->sources[i], rendered); free((void *)rendered); }

        int status, code;
        bool ok = neocmd_run_sync(cmd, &status, &code, false);
        neocmd_delete(cmd);

        if (!ok || code != CLD_EXITED || status != 0) {
            NEO_LOGF(NEO_LOG_ERROR, "Failed to compile '%s'", t->sources[i]);
            free(obj); neostr_free(&full_cflags); free(ccache_prefix);
            return false;
        }
        NEO_LOGF(NEO_LOG_INFO, "Compiled '%s'", t->sources[i]);
        free(obj);
    }
    neostr_free(&full_cflags);
    free(ccache_prefix);

    /* link/archive */
    neocmd_t *cmd = neocmd_create(NEO_SH);

    if (t->type == NEO_TARGET_STATIC_LIB) {
        neocmd_append(cmd, neo__ar_cmd(), "rcs", t->name);
    } else if (t->type == NEO_TARGET_SHARED_LIB) {
#ifdef NEO_PLATFORM_MACOS
        neocmd_append(cmd, effective_cc, "-dynamiclib", "-o", t->name);
#else
        neocmd_append(cmd, effective_cc, "-shared", "-o", t->name);
#endif
    } else {
        neocmd_append(cmd, effective_cc, "-o", t->name);
    }

    for (size_t i = 0; i < t->nsources; i++) {
        char *obj = neo__make_output_path(t->sources[i], NULL);
        if (obj) { neocmd_append(cmd, obj); free(obj); }
    }

    /* link dependency outputs (libraries) */
    for (size_t i = 0; i < t->ndeps; i++) {
        neo_target_t *dep = g->targets[t->deps[i]];
        if (dep->type == NEO_TARGET_STATIC_LIB || dep->type == NEO_TARGET_SHARED_LIB)
            neocmd_append(cmd, dep->name);
    }

    if (sysroot_flag[0] && t->type != NEO_TARGET_STATIC_LIB)
        neocmd_append(cmd, sysroot_flag);
    if (t->ldflags) neocmd_append(cmd, t->ldflags);

    int status, code;
    bool result = neocmd_run_sync(cmd, &status, &code, false);
    neocmd_delete(cmd);

    if (!result || code != CLD_EXITED || status != 0) {
        NEO_LOGF(NEO_LOG_ERROR, "Failed to build target '%s'", t->name);
        return false;
    }

    /* versioned shared lib symlinks */
    if (t->type == NEO_TARGET_SHARED_LIB && t->has_version) {
#ifndef NEO_PLATFORM_MACOS
        char versioned[PATH_MAX], soname[PATH_MAX];
        snprintf(versioned, sizeof(versioned), "%s.%d.%d.%d",
                 t->name, t->version_major, t->version_minor, t->version_patch);
        snprintf(soname, sizeof(soname), "%s.%d", t->name, t->version_major);
        rename(t->name, versioned);
        if (symlink(versioned, soname) == -1)
            NEO_LOGF(NEO_LOG_WARNING, "symlink '%s' -> '%s' failed", soname, versioned);
        if (symlink(versioned, t->name) == -1)
            NEO_LOGF(NEO_LOG_WARNING, "symlink '%s' -> '%s' failed", t->name, versioned);
#endif
    }

    NEO_LOGF(NEO_LOG_INFO, "Built target '%s'", t->name);
    return true;
}

/* Toposort + parallel build */
int neo_graph_build(neo_graph_t *g)
{
    if (!g || g->ntargets == 0) return 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    size_t n = g->ntargets;

    /* compute in-degrees */
    uint32_t *indeg = (uint32_t *)calloc(n, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        g->targets[i]->state = NEO_TSTATE_PENDING;
        for (size_t j = 0; j < g->targets[i]->ndeps; j++)
            indeg[g->targets[i]->deps[j]]++; /* wait... deps point to what this target depends ON */
    }
    /* fix: indeg[i] = number of targets that i depends on that are not yet done */
    memset(indeg, 0, n * sizeof(uint32_t));
    for (size_t i = 0; i < n; i++)
        indeg[i] = (uint32_t)g->targets[i]->ndeps;

    /* ready queue: targets with indeg 0 */
    uint32_t *queue = (uint32_t *)malloc(n * sizeof(uint32_t));
    size_t qhead = 0, qtail = 0;
    for (size_t i = 0; i < n; i++)
        if (indeg[i] == 0) { queue[qtail++] = (uint32_t)i; g->targets[i]->state = NEO_TSTATE_READY; }

    typedef struct { pid_t pid; uint32_t tid; } build_slot_t;
    build_slot_t *slots = (build_slot_t *)calloc((size_t)g_max_jobs, sizeof(build_slot_t));
    int active = 0, done = 0, failed = 0;

    while (qhead < qtail || active > 0) {
        /* launch ready targets */
        while (active < g_max_jobs && qhead < qtail) {
            uint32_t tid = queue[qhead++];
            neo_target_t *t = g->targets[tid];

            if (g_dry_run) {
                NEO_LOGF(NEO_LOG_INFO, "[DRY-RUN] Would build target '%s'", t->name);
                t->state = NEO_TSTATE_DONE;
                done++;
                /* unblock dependents */
                for (size_t i = 0; i < n; i++) {
                    for (size_t j = 0; j < g->targets[i]->ndeps; j++) {
                        if (g->targets[i]->deps[j] == tid) {
                            indeg[i]--;
                            if (indeg[i] == 0 && g->targets[i]->state == NEO_TSTATE_PENDING) {
                                g->targets[i]->state = NEO_TSTATE_READY;
                                queue[qtail++] = (uint32_t)i;
                            }
                        }
                    }
                }
                continue;
            }

            /* fork a child to build this target */
            pid_t p = fork();
            if (p == -1) { t->state = NEO_TSTATE_FAILED; failed++; continue; }
            if (p == 0) {
                /* child builds the target */
                bool ok = neo__build_single_target(g, t);
                _exit(ok ? 0 : 1);
            }
            t->state = NEO_TSTATE_BUILDING;
            t->build_pid = p;
            slots[active].pid = p;
            slots[active].tid = tid;
            active++;
        }

        if (active == 0) break;

        /* wait for any build to finish */
        int wstatus;
        pid_t fin = waitpid(-1, &wstatus, 0);
        if (fin <= 0) continue;

        for (int s = 0; s < active; s++) {
            if (slots[s].pid == fin) {
                uint32_t tid = slots[s].tid;
                neo_target_t *t = g->targets[tid];
                bool success = WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;

                if (success) {
                    t->state = NEO_TSTATE_DONE;
                    done++;
                    /* unblock dependents */
                    for (size_t i = 0; i < n; i++) {
                        for (size_t j = 0; j < g->targets[i]->ndeps; j++) {
                            if (g->targets[i]->deps[j] == tid) {
                                indeg[i]--;
                                if (indeg[i] == 0 && g->targets[i]->state == NEO_TSTATE_PENDING) {
                                    g->targets[i]->state = NEO_TSTATE_READY;
                                    queue[qtail++] = (uint32_t)i;
                                }
                            }
                        }
                    }
                } else {
                    t->state = NEO_TSTATE_FAILED;
                    failed++;
                    NEO_LOGF(NEO_LOG_ERROR, "Target '%s' failed", t->name);
                }
                slots[s] = slots[active - 1];
                active--;
                break;
            }
        }
    }

    /* check for cycles */
    if (done + failed < (int)n) {
        NEO_LOGF(NEO_LOG_ERROR, "Dependency cycle detected! %d targets could not be built",
                 (int)n - done - failed);
    }

    double elapsed = neo__elapsed_ms(&start);
    NEO_LOGF(NEO_LOG_INFO, "Build complete: %d/%zu targets in %.1fms (%d failed)",
             done, n, elapsed, failed);

    free(indeg); free(queue); free(slots);
    return done;
}

int neo_graph_build_target(neo_graph_t *g, const char *target_name)
{
    if (!g || !target_name) return -1;
    neo_target_t *t = neo_graph_find(g, target_name);
    if (!t) { NEO_LOGF(NEO_LOG_ERROR, "Target '%s' not found", target_name); return -1; }

    /* mark only this target and its transitive deps */
    bool *needed = (bool *)calloc(g->ntargets, sizeof(bool));
    /* BFS from target backwards through deps */
    uint32_t *stack = (uint32_t *)malloc(g->ntargets * sizeof(uint32_t));
    size_t sp = 0;
    stack[sp++] = t->id;
    needed[t->id] = true;
    while (sp > 0) {
        uint32_t id = stack[--sp];
        for (size_t i = 0; i < g->targets[id]->ndeps; i++) {
            uint32_t dep = g->targets[id]->deps[i];
            if (!needed[dep]) {
                needed[dep] = true;
                stack[sp++] = dep;
            }
        }
    }
    free(stack);

    /* temporarily remove unneeded targets from the build */
    /* simplest approach: just build all needed targets using neo_graph_build
       but mark unneeded ones as DONE */
    for (size_t i = 0; i < g->ntargets; i++) {
        if (!needed[i]) g->targets[i]->state = NEO_TSTATE_DONE;
    }
    free(needed);

    /* But we need a cleaner approach: just build the target directly */
    /* Actually, let's build deps first recursively */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    /* reset states for needed targets */
    /* Use the full graph_build which handles everything */
    /* Reset states */
    for (size_t i = 0; i < g->ntargets; i++) {
        if (g->targets[i]->state != NEO_TSTATE_DONE)
            g->targets[i]->state = NEO_TSTATE_PENDING;
    }

    return neo_graph_build(g);
}

/* ════════════════════════════════════════════════════════════════
 *  Graph Export (DOT format)
 * ════════════════════════════════════════════════════════════════ */

bool neo_graph_export_dot(neo_graph_t *g, const char *output_path)
{
    if (!g || !output_path) return false;
    neostr_t dot = neostr_empty();
    neostr_append(&dot, "digraph neobuild {\n  rankdir=LR;\n  node [shape=box, style=filled];\n\n");

    for (size_t i = 0; i < g->ntargets; i++) {
        neo_target_t *t = g->targets[i];
        const char *color;
        switch (t->type) {
        case NEO_TARGET_EXECUTABLE: color = "#4A90D9"; break;
        case NEO_TARGET_STATIC_LIB: color = "#7BC67E"; break;
        case NEO_TARGET_SHARED_LIB: color = "#F5A623"; break;
        case NEO_TARGET_CUSTOM:     color = "#CCCCCC"; break;
        default:                    color = "#FFFFFF"; break;
        }
        neostr_appendf(&dot, "  \"%s\" [fillcolor=\"%s\", label=\"%s\\n(%s)\"];\n",
                       t->name, color, t->name,
                       t->type == NEO_TARGET_EXECUTABLE ? "exe" :
                       t->type == NEO_TARGET_STATIC_LIB ? "static" :
                       t->type == NEO_TARGET_SHARED_LIB ? "shared" : "custom");
    }
    neostr_append(&dot, "\n");

    for (size_t i = 0; i < g->ntargets; i++) {
        neo_target_t *t = g->targets[i];
        for (size_t j = 0; j < t->ndeps; j++)
            neostr_appendf(&dot, "  \"%s\" -> \"%s\";\n",
                           t->name, g->targets[t->deps[j]]->name);
    }

    neostr_append(&dot, "}\n");
    FILE *f = fopen(output_path, "w");
    if (!f) { neostr_free(&dot); return false; }
    fputs(dot.data ? dot.data : "", f);
    fclose(f);
    neostr_free(&dot);
    NEO_LOGF(NEO_LOG_INFO, "Exported build graph to '%s'", output_path);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Package / Feature Detection
 * ════════════════════════════════════════════════════════════════ */

neo_package_t neo_find_package(const char *name)
{
    neo_package_t pkg = {NULL, NULL, NULL, false};
    if (!name) return pkg;

    const char *cflags_argv[] = {"pkg-config", "--cflags", name, NULL};
    const char *libs_argv[]   = {"pkg-config", "--libs", name, NULL};
    const char *ver_argv[]    = {"pkg-config", "--modversion", name, NULL};

    pkg.cflags = neo__capture_output(cflags_argv);
    pkg.libs = neo__capture_output(libs_argv);
    pkg.version = neo__capture_output(ver_argv);
    pkg.found = (pkg.libs != NULL && strlen(pkg.libs) > 0);

    return pkg;
}

void neo_package_free(neo_package_t *pkg)
{
    if (!pkg) return;
    free(pkg->cflags); free(pkg->libs); free(pkg->version);
    pkg->cflags = pkg->libs = pkg->version = NULL;
    pkg->found = false;
}

void neo_target_use_package(neo_target_t *t, const neo_package_t *pkg)
{
    if (!t || !pkg || !pkg->found) return;
    if (pkg->cflags) neo_target_add_cflags(t, pkg->cflags);
    if (pkg->libs) neo_target_add_ldflags(t, pkg->libs);
}

static bool neo__try_compile(const char *code)
{
    char tmpfile[] = "/tmp/neo_probe_XXXXXX.c";
    int fd = mkstemps(tmpfile, 2);
    if (fd < 0) return false;
    if (write(fd, code, strlen(code)) < 0) { close(fd); unlink(tmpfile); return false; }
    close(fd);

    char obj[PATH_MAX];
    snprintf(obj, sizeof(obj), "%.*s.o", (int)(strlen(tmpfile) - 2), tmpfile);

    const char *argv[] = {"cc", "-c", tmpfile, "-o", obj, "-w", NULL};
    neo_process_t proc = neo__spawn(argv);
    int ret = neo__wait(proc);

    unlink(tmpfile);
    unlink(obj);
    return ret == 0;
}

static bool neo__try_link(const char *code, const char *lib_flag)
{
    char tmpfile[] = "/tmp/neo_probe_XXXXXX.c";
    int fd = mkstemps(tmpfile, 2);
    if (fd < 0) return false;
    if (write(fd, code, strlen(code)) < 0) { close(fd); unlink(tmpfile); return false; }
    close(fd);

    char outfile[PATH_MAX];
    snprintf(outfile, sizeof(outfile), "%.*s", (int)(strlen(tmpfile) - 2), tmpfile);

    const char *argv[] = {"cc", tmpfile, "-o", outfile, lib_flag, "-w", NULL};
    neo_process_t proc = neo__spawn(argv);
    int ret = neo__wait(proc);

    unlink(tmpfile);
    unlink(outfile);
    return ret == 0;
}

bool neo_check_header(const char *header)
{
    if (!header) return false;
    char code[512];
    snprintf(code, sizeof(code), "#include <%s>\nint main(void){return 0;}\n", header);
    return neo__try_compile(code);
}

bool neo_check_lib(const char *lib)
{
    if (!lib) return false;
    char flag[256];
    snprintf(flag, sizeof(flag), "-l%s", lib);
    return neo__try_link("int main(void){return 0;}\n", flag);
}

bool neo_check_symbol(const char *lib, const char *symbol)
{
    if (!lib || !symbol) return false;
    char code[512];
    snprintf(code, sizeof(code), "extern void %s(void);\nint main(void){%s();return 0;}\n", symbol, symbol);
    char flag[256];
    snprintf(flag, sizeof(flag), "-l%s", lib);
    return neo__try_link(code, flag);
}

bool neo_generate_pkg_config(const char *output_path, const char *name,
    const char *version, const char *description,
    const char *cflags, const char *libs)
{
    if (!output_path || !name) return false;
    FILE *f = fopen(output_path, "w");
    if (!f) return false;

    fprintf(f, "prefix=/usr/local\n");
    fprintf(f, "exec_prefix=${prefix}\n");
    fprintf(f, "libdir=${exec_prefix}/lib\n");
    fprintf(f, "includedir=${prefix}/include\n\n");
    fprintf(f, "Name: %s\n", name);
    if (description) fprintf(f, "Description: %s\n", description);
    if (version) fprintf(f, "Version: %s\n", version);
    if (cflags) fprintf(f, "Cflags: %s\n", cflags);
    if (libs) fprintf(f, "Libs: %s\n", libs);

    fclose(f);
    NEO_LOGF(NEO_LOG_INFO, "Generated pkg-config file '%s'", output_path);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Install
 * ════════════════════════════════════════════════════════════════ */

neo_install_dirs_t neo_install_dirs_default(const char *prefix)
{
    neo_install_dirs_t d;
    memset(&d, 0, sizeof(d));
    const char *pfx = prefix ? prefix : "/usr/local";
    snprintf(d.prefix, sizeof(d.prefix), "%s", pfx);
    snprintf(d.bindir, sizeof(d.bindir), "%s/bin", pfx);
    snprintf(d.libdir, sizeof(d.libdir), "%s/lib", pfx);
    snprintf(d.includedir, sizeof(d.includedir), "%s/include", pfx);
    snprintf(d.pkgconfigdir, sizeof(d.pkgconfigdir), "%s/lib/pkgconfig", pfx);
    return d;
}

bool neo_install_target(neo_graph_t *g, const char *target_name,
    const neo_install_dirs_t *dirs)
{
    if (!g || !target_name || !dirs) return false;
    neo_target_t *t = neo_graph_find(g, target_name);
    if (!t) return false;

    const char *dest_dir;
    switch (t->type) {
    case NEO_TARGET_EXECUTABLE: dest_dir = dirs->bindir; break;
    case NEO_TARGET_STATIC_LIB:
    case NEO_TARGET_SHARED_LIB: dest_dir = dirs->libdir; break;
    default: return false;
    }

    neo_mkdir(dest_dir, 0755);

    char dest[PATH_MAX];
    const char *basename = strrchr(t->name, '/');
    basename = basename ? basename + 1 : t->name;
    snprintf(dest, sizeof(dest), "%s/%s", dest_dir, basename);

    const char *argv[] = {"install", "-m",
        (t->type == NEO_TARGET_EXECUTABLE) ? "755" : "644",
        t->name, dest, NULL};
    neo_process_t proc = neo__spawn(argv);
    int ret = neo__wait(proc);

    if (ret == 0) NEO_LOGF(NEO_LOG_INFO, "Installed '%s' -> '%s'", t->name, dest);
    else NEO_LOGF(NEO_LOG_ERROR, "Failed to install '%s'", t->name);
    return ret == 0;
}

bool neo_install_headers(const char **headers, size_t nheaders,
    const char *subdir, const neo_install_dirs_t *dirs)
{
    if (!headers || !dirs) return false;
    char dest_dir[PATH_MAX];
    if (subdir)
        snprintf(dest_dir, sizeof(dest_dir), "%s/%s", dirs->includedir, subdir);
    else
        snprintf(dest_dir, sizeof(dest_dir), "%s", dirs->includedir);

    neo_mkdir(dest_dir, 0755);

    for (size_t i = 0; i < nheaders; i++) {
        const char *bname = strrchr(headers[i], '/');
        bname = bname ? bname + 1 : headers[i];
        size_t needed = strlen(dest_dir) + 1 + strlen(bname) + 1;
        char *dest = (char *)malloc(needed);
        if (!dest) return false;
        snprintf(dest, needed, "%s/%s", dest_dir, bname);

        const char *argv[] = {"install", "-m", "644", headers[i], dest, NULL};
        neo_process_t proc = neo__spawn(argv);
        int ret = neo__wait(proc);
        free(dest);
        if (ret != 0) {
            NEO_LOGF(NEO_LOG_ERROR, "Failed to install header '%s'", headers[i]);
            return false;
        }
    }
    NEO_LOGF(NEO_LOG_INFO, "Installed %zu headers to '%s'", nheaders, dest_dir);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Test Runner
 * ════════════════════════════════════════════════════════════════ */

typedef struct {
    char *name;
    char *command;
} neo_test_entry_t;

struct neo_test_suite {
    char *name;
    neo_test_entry_t *tests;
    size_t count;
    size_t cap;
    int timeout_sec;
};

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

/* ════════════════════════════════════════════════════════════════
 *  Filesystem
 * ════════════════════════════════════════════════════════════════ */

bool neo_mkdir(const char *dir_path, mode_t mode)
{
    if (!dir_path) return false;
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    size_t len = strlen(tmp);
    if (len > 1 && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) == -1 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) == -1 && errno != EEXIST) return false;
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Self-Rebuild
 * ════════════════════════════════════════════════════════════════ */

bool neorebuild(const char *build_file_c, char **argv, int *argc)
{
    if (!argv) return true;
    char **temp = argv + 1;
    while (*temp) {
        if (strcmp(*temp, "--no-rebuild") == 0) { (*argc)--; return true; }
        temp++;
    }
    if (!build_file_c) return false;

    struct stat src_stat;
    if (stat(build_file_c, &src_stat) == -1) return false;

    size_t len = strlen(build_file_c);
    char *build_file = (char *)malloc(len + 1);
    if (!build_file) return false;
    strcpy(build_file, build_file_c);
    char *ext = strrchr(build_file, '.');
    if (ext && strcmp(ext, ".c") == 0) *ext = '\0';

    struct stat bin_stat;
    if (stat(build_file, &bin_stat) == -1) { free(build_file); return false; }
    if (bin_stat.st_mtime >= src_stat.st_mtime) { free(build_file); return true; }

    NEO_LOGF(NEO_LOG_INFO, "'%s' modified — rebuilding", build_file_c);

    char cmd[PATH_MAX + 32];
    snprintf(cmd, sizeof(cmd), "./buildneo %s", build_file_c);
    if (system(cmd) == -1) { free(build_file); return false; }

    neocmd_t *neo = neocmd_create(NEO_SH);
    if (!neo) { free(build_file); return false; }

    char run_cmd[PATH_MAX + 4];
    snprintf(run_cmd, sizeof(run_cmd), "./%s", build_file);
    neocmd_append(neo, run_cmd);
    char **arg_ptr = argv + 1;
    while (*arg_ptr) {
        char buf[PATH_MAX + 4];
        snprintf(buf, sizeof(buf), "\"%s\"", *arg_ptr);
        neocmd_append(neo, buf);
        arg_ptr++;
    }
    neocmd_append(neo, "--no-rebuild");

    if (!neocmd_run_sync(neo, NULL, NULL, false)) {
        free(build_file); neocmd_delete(neo); return false;
    }
    free(build_file); neocmd_delete(neo);
    exit(EXIT_SUCCESS);
    return true;
}

/* ════════════════════════════════════════════════════════════════
 *  Configuration Parsing
 * ════════════════════════════════════════════════════════════════ */

neoconfig_t *neo_parse_config(const char *config_file_path, size_t *config_num)
{
    if (!config_file_path || !config_num) return NULL;
    neostr_t file = neostr_read_file(config_file_path);
    if (!file.data) return NULL;

    size_t parts_count = 0;
    char **parts = neostr_split(file.data, ';', &parts_count);
    neostr_free(&file);
    if (!parts || parts_count == 0) return NULL;

    neoconfig_t *config = (neoconfig_t *)malloc(parts_count * sizeof(neoconfig_t));
    if (!config) { neostr_free_split(parts, parts_count); return NULL; }

    size_t valid = 0;
    for (size_t i = 0; i < parts_count; i++) {
        char *eq = strchr(parts[i], '=');
        if (!eq) continue;
        *eq = '\0';
        const char *kr = parts[i], *vr = eq + 1;
        while (*kr && isspace(*kr)) kr++;
        while (*vr && isspace(*vr)) vr++;
        const char *ke = eq - 1, *ve = vr + strlen(vr) - 1;
        while (ke > kr && isspace(*ke)) ke--;
        while (ve > vr && isspace(*ve)) ve--;
        size_t kl = (size_t)(ke - kr) + 1, vl = (size_t)(ve - vr) + 1;
        config[valid].key = (char *)malloc(kl + 1);
        config[valid].value = (char *)malloc(vl + 1);
        if (!config[valid].key || !config[valid].value) { free(config[valid].key); free(config[valid].value); continue; }
        memcpy(config[valid].key, kr, kl); config[valid].key[kl] = '\0';
        memcpy(config[valid].value, vr, vl); config[valid].value[vl] = '\0';
        valid++;
    }
    neostr_free_split(parts, parts_count);
    if (valid == 0) { free(config); *config_num = 0; return NULL; }
    *config_num = valid;
    return config;
}

bool neo_free_config(neoconfig_t *config_arr, size_t config_num)
{
    if (!config_arr) return false;
    for (size_t i = 0; i < config_num; i++) { free(config_arr[i].key); free(config_arr[i].value); }
    free(config_arr);
    return true;
}

neoconfig_t *neo_parse_config_arg(char **argv, size_t *config_arr_len)
{
    if (!argv || !config_arr_len) return NULL;
    for (char **p = argv + 1; *p; p++) {
        if (strncmp(*p, "--config=", 9) == 0)
            return neo_parse_config(*p + 9, config_arr_len);
    }
    return NULL;
}
