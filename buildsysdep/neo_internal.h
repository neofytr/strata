#ifndef NEO_INTERNAL_H
#define NEO_INTERNAL_H

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

/* macOS: sysctlbyname is in sys/types.h + sys/sysctl.h but sysctl.h
   conflicts with _POSIX_C_SOURCE due to BSD types. Just declare it. */
#ifdef __APPLE__
extern int sysctlbyname(const char *, void *, size_t *, void *, size_t);
#endif

/* ================================================================
 *  Global State (defined in neo_core.c)
 * ================================================================ */

extern neocompiler_t g_default_compiler;
extern neoverbosity_t g_verbosity;
extern neoprofile_t g_profile;
extern bool g_dry_run;
extern int g_max_jobs;
extern char g_build_dir[PATH_MAX];
extern vector_t g_compile_commands;

/* ================================================================
 *  compile_commands.json entry type
 * ================================================================ */

typedef struct {
    char *directory;
    char *command;
    char *file;
} neo_cc_entry_t;

/* ================================================================
 *  neostr_t -- Minimal String Builder (internal only)
 * ================================================================ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} neostr_t;

static inline neostr_t neostr_empty(void) { return (neostr_t){NULL, 0, 0}; }

static inline void neostr_free(neostr_t *s)
{
    free(s->data);
    s->data = NULL;
    s->len = s->cap = 0;
}

static inline bool neostr_ensure(neostr_t *s, size_t needed)
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

static inline bool neostr_append(neostr_t *s, const char *cstr)
{
    if (!cstr) return true;
    size_t add = strlen(cstr);
    if (!neostr_ensure(s, s->len + add + 1)) return false;
    memcpy(s->data + s->len, cstr, add + 1);
    s->len += add;
    return true;
}

static inline bool neostr_appendf(neostr_t *s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static inline bool neostr_appendf(neostr_t *s, const char *fmt, ...)
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

static inline char *neostr_to_cstr(neostr_t *s)
{
    if (!s->data) return strdup("");
    return strdup(s->data);
}

/* Larger neostr functions (defined in neo_core.c) */
neostr_t neostr_read_file(const char *path);
char   **neostr_split(const char *str, char delim, size_t *out_count);
void     neostr_free_split(char **arr, size_t count);

/* ================================================================
 *  Process type
 * ================================================================ */

typedef struct { pid_t pid; } neo_process_t;

/* ================================================================
 *  Arena Allocator internals
 * ================================================================ */

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

/* ================================================================
 *  Build Database types
 * ================================================================ */

#define NEO_BUILDDB_MAGIC   0x4E454F4442ULL  /* "NEODB" */
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

/* ================================================================
 *  Target Graph types
 * ================================================================ */

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

/* ================================================================
 *  Toolchain
 * ================================================================ */

struct neo_toolchain {
    char *prefix;
    char *sysroot;
    char *cc;
    char *cxx;
    char *ar;
    char *ranlib;
};

/* ================================================================
 *  Test Runner types
 * ================================================================ */

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

/* ================================================================
 *  Header Dependency Tracking
 * ================================================================ */

#define NEO_MAX_DEPS 256

/* ================================================================
 *  Cross-file internal function declarations
 * ================================================================ */

/* neo_core.c */
const char *neo__level_str(neolog_level_t level);
const char *neo__level_color(neolog_level_t level);
const char *neo__compiler_cmd(neocompiler_t c);
neocompiler_t neo__resolve_compiler(neocompiler_t c);
const char *neo__profile_flags(void);
double neo__elapsed_ms(struct timespec *start);

/* neo_platform.c */
neo_process_t neo__spawn(const char *const argv[]);
neo_process_t neo__spawn_shell(neoshell_t shell, const char *cmdline);
int neo__wait(neo_process_t proc);
neo_process_t neo__wait_any(int *exit_code);
char *neo__capture_output(const char *const argv[]);
const char *neo__ar_cmd(void);
const char *neo__ranlib_cmd(void);

/* neo_deps.c */
uint64_t neo__fnv1a(const void *data, size_t len);
uint64_t neo__fnv1a_str(const char *s);
uint64_t neo__hash_file(const char *path);
bool neo__scan_includes(const char *filepath, time_t output_mtime,
                        const char **visited, size_t *visited_count, bool *newer,
                        const char **include_dirs, size_t ninclude_dirs);
bool neo__needs_recompile_ex(const char *source, const char *output,
                             const char **include_dirs, size_t ninclude_dirs);
neo_builddb_t *neo__builddb_load(const char *path);
bool neo__builddb_save(neo_builddb_t *db);
bool neo__builddb_needs_rebuild(neo_builddb_t *db, const char *source,
                                const char *flags, const char *compiler);
void neo__builddb_update(neo_builddb_t *db, const char *source,
                         const char *flags, const char *compiler);
void neo__builddb_free(neo_builddb_t *db);

/* neo_compile.c */
void neo__record_compile_command(const char *source, const char *cmd_str);
void neo__json_escape(neostr_t *s, const char *str);
char *neo__make_output_path(const char *source, const char *output);

/* neo_graph.c */
neo_target_t *neo__graph_add(neo_graph_t *g, const char *name,
                             neo_target_type_t type, const char **sources,
                             size_t nsources);

#endif /* NEO_INTERNAL_H */
