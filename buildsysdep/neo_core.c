#include "neo_internal.h"

/* ================================================================
 *  Global State (definitions)
 * ================================================================ */

neocompiler_t g_default_compiler = NEO_GCC;
neoverbosity_t g_verbosity       = NEO_NORMAL;
neoprofile_t g_profile           = NEO_PROFILE_NONE;
bool g_dry_run                   = false;
int g_max_jobs                   = 1;
char g_build_dir[PATH_MAX]       = {0};

/* compile_commands.json accumulator */
vector_t g_compile_commands = NEOVEC_INIT;

/* ================================================================
 *  Logging helpers
 * ================================================================ */

const char *neo__level_str(neolog_level_t level)
{
    switch (level) {
    case NEO_LOG_ERROR:   return "ERROR";
    case NEO_LOG_WARNING: return "WARNING";
    case NEO_LOG_INFO:    return "INFO";
    case NEO_LOG_DEBUG:   return "DEBUG";
    default:              return "UNKNOWN";
    }
}

const char *neo__level_color(neolog_level_t level)
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

/* ================================================================
 *  Global Settings
 * ================================================================ */

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
    if (n <= 0) {
#if defined(__APPLE__)
        int ncpu = 1;
        size_t len = sizeof(ncpu);
        sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0);
        n = ncpu;
#elif defined(_SC_NPROCESSORS_ONLN)
        n = (int)sysconf(_SC_NPROCESSORS_ONLN);
#else
        n = 1;
#endif
    }
    if (n <= 0) n = 1;
    g_max_jobs = n;
}

/* ================================================================
 *  Compiler Helpers
 * ================================================================ */

const char *neo__compiler_cmd(neocompiler_t c)
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

neocompiler_t neo__resolve_compiler(neocompiler_t c)
{
    return (c == NEO_GLOBAL_DEFAULT) ? g_default_compiler : c;
}

const char *neo__profile_flags(void)
{
    switch (g_profile) {
    case NEO_PROFILE_DEBUG:   return "-g -O0 -fsanitize=address -fsanitize=undefined";
    case NEO_PROFILE_RELEASE: return "-O3 -DNDEBUG -march=native";
    case NEO_PROFILE_RELDBG:  return "-O2 -g";
    default:                  return NULL;
    }
}

double neo__elapsed_ms(struct timespec *start)
{
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    return (end.tv_sec - start->tv_sec) * 1000.0 + (end.tv_nsec - start->tv_nsec) / 1e6;
}

/* ================================================================
 *  Larger neostr functions
 * ================================================================ */

neostr_t neostr_read_file(const char *path)
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

char **neostr_split(const char *str, char delim, size_t *out_count)
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

void neostr_free_split(char **arr, size_t count)
{
    if (!arr) return;
    for (size_t i = 0; i < count; i++) free(arr[i]);
    free(arr);
}
