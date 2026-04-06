#ifndef NEOBUILD_H
#define NEOBUILD_H

#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdio.h>

#include "neovec/neovec.h"

/* ── Platform Detection ─────────────────────────────────────────── */
#if defined(__APPLE__) && defined(__MACH__)
  #define NEO_PLATFORM_MACOS 1
#elif defined(__linux__)
  #define NEO_PLATFORM_LINUX 1
#endif

/* ── Compiler Types ─────────────────────────────────────────────── */
typedef enum
{
    NEO_LD,
    NEO_AS,
    NEO_GCC,
    NEO_CLANG,
    NEO_GPP,
    NEO_CLANGPP,
    NEO_GLOBAL_DEFAULT,
} neocompiler_t;

/* ── Log Levels ─────────────────────────────────────────────────── */
typedef enum
{
    NEO_LOG_ERROR,
    NEO_LOG_WARNING,
    NEO_LOG_INFO,
    NEO_LOG_DEBUG,
} neolog_level_t;

/* ── Verbosity ──────────────────────────────────────────────────── */
typedef enum
{
    NEO_QUIET,
    NEO_NORMAL,
    NEO_VERBOSE,
} neoverbosity_t;

/* ── Build Profiles ─────────────────────────────────────────────── */
typedef enum
{
    NEO_PROFILE_NONE,
    NEO_PROFILE_DEBUG,
    NEO_PROFILE_RELEASE,
    NEO_PROFILE_RELDBG,
} neoprofile_t;

/* ── Shell Types ────────────────────────────────────────────────── */
typedef enum
{
    NEO_DASH,
    NEO_BASH,
    NEO_SH,
    NEO_DIRECT,   /* no shell — direct execvp */
} neoshell_t;

/* ── Command ────────────────────────────────────────────────────── */
typedef struct
{
    vector_t args;
    neoshell_t shell;
} neocmd_t;

/* ── Config ─────────────────────────────────────────────────────── */
typedef struct
{
    char *key;
    char *value;
} neoconfig_t;

/* ── Arena Allocator ────────────────────────────────────────────── */
typedef struct neo_arena neo_arena_t;

neo_arena_t *neo_arena_create(size_t page_size);
void        *neo_arena_alloc(neo_arena_t *a, size_t size);
char        *neo_arena_strdup(neo_arena_t *a, const char *s);
char        *neo_arena_sprintf(neo_arena_t *a, const char *fmt, ...)
              __attribute__((format(printf, 2, 3)));
void         neo_arena_destroy(neo_arena_t *a);

/* ── Formatted Logging ──────────────────────────────────────────── */
#define NEO_LOGF(level, fmt, ...) \
    neo__logf((level), (fmt), ##__VA_ARGS__)

void neo__logf(neolog_level_t level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* ── Global Settings ────────────────────────────────────────────── */
void neo_set_global_default_compiler(neocompiler_t compiler);
neocompiler_t neo_get_global_default_compiler(void);
void neo_set_verbosity(neoverbosity_t v);
void neo_set_dry_run(bool enabled);
void neo_set_build_dir(const char *dir);
void neo_set_profile(neoprofile_t profile);
void neo_set_jobs(int n);

/* ── Command API ────────────────────────────────────────────────── */
neocmd_t *neocmd_create(neoshell_t shell);
bool neocmd_delete(neocmd_t *neocmd);
pid_t neocmd_run_async(neocmd_t *neocmd);
bool neocmd_run_sync(neocmd_t *neocmd, int *status, int *code, bool print_status_desc);
bool neocmd_append_null(neocmd_t *neocmd, ...);
const char *neocmd_render(neocmd_t *neocmd);
bool neoshell_wait(pid_t pid, int *status, int *code, bool should_print);

#define neocmd_append(neocmd_ptr, ...) \
    neocmd_append_null((neocmd_ptr), __VA_ARGS__, NULL)

/* ── Compilation / Linking ──────────────────────────────────────── */
bool neo_compile_to_object_file(neocompiler_t compiler, const char *source,
    const char *output, const char *compiler_flags, bool force_compilation);

int neo_compile_parallel(neocompiler_t compiler, const char **sources,
    size_t count, const char *compiler_flags, bool force_compilation);

#define neo_link(compiler, executable, linker_flags, forced, ...) \
    neo_link_null((compiler), (executable), (linker_flags), (forced), __VA_ARGS__, NULL)

bool neo_link_null(neocompiler_t compiler, const char *executable,
    const char *linker_flags, int forced_linking, ...);

/* ── Library Building ───────────────────────────────────────────── */
bool neo_build_static_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags);
bool neo_build_shared_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags,
    const char *ldflags);

/* ── Header Dependency Tracking ─────────────────────────────────── */
bool neo_needs_recompile(const char *source, const char *output);

/* ── compile_commands.json ──────────────────────────────────────── */
bool neo_export_compile_commands(const char *output_path);

/* ── Target Graph ───────────────────────────────────────────────── */
typedef enum
{
    NEO_TARGET_EXECUTABLE,
    NEO_TARGET_STATIC_LIB,
    NEO_TARGET_SHARED_LIB,
    NEO_TARGET_OBJECT,
    NEO_TARGET_CUSTOM,
} neo_target_type_t;

typedef struct neo_target neo_target_t;
typedef struct neo_graph neo_graph_t;

neo_graph_t  *neo_graph_create(void);
void          neo_graph_destroy(neo_graph_t *g);

neo_target_t *neo_add_executable(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
neo_target_t *neo_add_static_lib(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
neo_target_t *neo_add_shared_lib(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
neo_target_t *neo_add_custom(neo_graph_t *g, const char *name,
                             const char *command);

void neo_target_set_compiler(neo_target_t *t, neocompiler_t compiler);
void neo_target_add_cflags(neo_target_t *t, const char *flags);
void neo_target_add_ldflags(neo_target_t *t, const char *flags);
void neo_target_add_include_dir(neo_target_t *t, const char *dir);
void neo_target_depends_on(neo_target_t *target, neo_target_t *dependency);
void neo_target_set_version(neo_target_t *t, int major, int minor, int patch);

int  neo_graph_build(neo_graph_t *g);
int  neo_graph_build_target(neo_graph_t *g, const char *target_name);
neo_target_t *neo_graph_find(neo_graph_t *g, const char *name);

/* ── Content Hash Database ──────────────────────────────────────── */
void neo_graph_enable_content_hash(neo_graph_t *g);

/* ── Compiler Cache ─────────────────────────────────────────────── */
void neo_graph_enable_ccache(neo_graph_t *g);

/* ── Graph Export ───────────────────────────────────────────────── */
bool neo_graph_export_dot(neo_graph_t *g, const char *output_path);

/* ── Package / Feature Detection ────────────────────────────────── */
typedef struct
{
    char *cflags;
    char *libs;
    char *version;
    bool found;
} neo_package_t;

neo_package_t neo_find_package(const char *name);
void          neo_package_free(neo_package_t *pkg);

bool neo_check_header(const char *header);
bool neo_check_lib(const char *lib);
bool neo_check_symbol(const char *lib, const char *symbol);

void neo_target_use_package(neo_target_t *t, const neo_package_t *pkg);

bool neo_generate_pkg_config(const char *output_path, const char *name,
    const char *version, const char *description,
    const char *cflags, const char *libs);

/* ── Toolchain / Cross-Compilation ──────────────────────────────── */
typedef struct neo_toolchain neo_toolchain_t;

neo_toolchain_t *neo_toolchain_create(const char *prefix);
void neo_toolchain_set_sysroot(neo_toolchain_t *tc, const char *sysroot);
void neo_toolchain_set_cc(neo_toolchain_t *tc, const char *cc);
void neo_toolchain_set_cxx(neo_toolchain_t *tc, const char *cxx);
void neo_toolchain_destroy(neo_toolchain_t *tc);

void neo_graph_set_toolchain(neo_graph_t *g, neo_toolchain_t *tc);
void neo_target_set_toolchain(neo_target_t *t, neo_toolchain_t *tc);

/* ── Install ────────────────────────────────────────────────────── */
typedef struct
{
    char prefix[512];
    char bindir[512];
    char libdir[512];
    char includedir[512];
    char pkgconfigdir[512];
} neo_install_dirs_t;

neo_install_dirs_t neo_install_dirs_default(const char *prefix);
bool neo_install_target(neo_graph_t *g, const char *target_name,
                        const neo_install_dirs_t *dirs);
bool neo_install_headers(const char **headers, size_t nheaders,
                         const char *subdir, const neo_install_dirs_t *dirs);

/* ── Test Runner ────────────────────────────────────────────────── */
typedef struct neo_test_suite neo_test_suite_t;

typedef struct
{
    int total;
    int passed;
    int failed;
    int crashed;
    int timed_out;
    double elapsed_ms;
} neo_test_results_t;

neo_test_suite_t *neo_test_suite_create(const char *name);
void neo_test_suite_add(neo_test_suite_t *suite, const char *test_name,
                        const char *command);
void neo_test_suite_set_timeout(neo_test_suite_t *suite, int seconds);
neo_test_results_t neo_test_suite_run(neo_test_suite_t *suite);
void neo_test_suite_destroy(neo_test_suite_t *suite);

/* ── Filesystem / Config ────────────────────────────────────────── */
bool neo_mkdir(const char *dir_path, mode_t mode);
bool neorebuild(const char *build_file, char **argv, int *argc);
neoconfig_t *neo_parse_config(const char *config_file_path, size_t *config_arr_len);
bool neo_free_config(neoconfig_t *config_arr, size_t config_arr_len);
neoconfig_t *neo_parse_config_arg(char **argv, size_t *config_arr_len);

#define LABEL_WITH_SPACES(label) #label

/* ── Backward Compatibility ─────────────────────────────────────── */
#ifndef NEO_NO_COMPAT

#define LD          NEO_LD
#define AS          NEO_AS
#define GCC         NEO_GCC
#define CLANG       NEO_CLANG
#define GLOBAL_DEFAULT NEO_GLOBAL_DEFAULT

#define ERROR       NEO_LOG_ERROR
#define WARNING     NEO_LOG_WARNING
#define INFO        NEO_LOG_INFO
#define DEBUG       NEO_LOG_DEBUG

#define DASH        NEO_DASH
#define BASH        NEO_BASH
#define SH          NEO_SH

#define NEO_LOG(level, msg) NEO_LOGF((level), "%s", (msg))

#endif /* NEO_NO_COMPAT */

/* ── Prefix-removal aliases ─────────────────────────────────────── */
#ifdef NEO_REMOVE_PREFIX

#define cmd_create      neocmd_create
#define cmd_delete      neocmd_delete
#define cmd_run_async   neocmd_run_async
#define cmd_run_sync    neocmd_run_sync
#define cmd_append      neocmd_append
#define cmd_append_null neocmd_append_null
#define cmd_render      neocmd_render
#define shell_wait      neoshell_wait

#endif /* NEO_REMOVE_PREFIX */

#endif /* NEOBUILD_H */
