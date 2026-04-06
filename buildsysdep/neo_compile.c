#include "neo_internal.h"

/* ================================================================
 *  compile_commands.json
 * ================================================================ */

void neo__record_compile_command(const char *source, const char *cmd_str)
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

void neo__json_escape(neostr_t *s, const char *str)
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

/* ================================================================
 *  Output Path Helper
 * ================================================================ */

char *neo__make_output_path(const char *source, const char *output)
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

/* ================================================================
 *  Compilation
 * ================================================================ */

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

/* ================================================================
 *  Library Building
 * ================================================================ */

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

/* ================================================================
 *  Linking
 * ================================================================ */

bool neo_link_null(neocompiler_t compiler, const char *executable,
    const char *linker_flags, int forced_linking, ...)
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
