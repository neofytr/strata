#include "neo_internal.h"

/* ================================================================
 *  Target Graph
 * ================================================================ */

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

neo_target_t *neo__graph_add(neo_graph_t *g, const char *name,
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

/* ================================================================
 *  Helper: extract -I dirs from a cflags string
 * ================================================================ */

static void neo__extract_include_dirs(const char *cflags,
                                      const char ***out_dirs, size_t *out_count)
{
    *out_dirs = NULL;
    *out_count = 0;
    if (!cflags) return;

    size_t cap = 4;
    const char **dirs = (const char **)malloc(cap * sizeof(const char *));
    if (!dirs) return;

    const char *p = cflags;
    while (*p) {
        while (*p && isspace(*p)) p++;
        if (strncmp(p, "-I", 2) == 0) {
            p += 2;
            /* skip optional space after -I */
            while (*p && isspace(*p)) p++;
            const char *start = p;
            while (*p && !isspace(*p)) p++;
            if (p > start) {
                size_t len = (size_t)(p - start);
                char *dir = (char *)malloc(len + 1);
                if (dir) {
                    memcpy(dir, start, len);
                    dir[len] = '\0';
                    if (*out_count >= cap) {
                        cap *= 2;
                        dirs = (const char **)realloc(dirs, cap * sizeof(const char *));
                    }
                    dirs[(*out_count)++] = dir;
                }
            }
        } else {
            while (*p && !isspace(*p)) p++;
        }
    }
    *out_dirs = dirs;
}

static void neo__free_include_dirs(const char **dirs, size_t count)
{
    if (!dirs) return;
    for (size_t i = 0; i < count; i++) free((void *)dirs[i]);
    free(dirs);
}

/* ================================================================
 *  Build a single target (FIX 1: in-process, no fork)
 *  (FIX 4: content hash integration)
 * ================================================================ */

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

    /* FIX: extract -I dirs from cflags for include scanner */
    const char **inc_dirs = NULL;
    size_t ninc_dirs = 0;
    neo__extract_include_dirs(full_cflags.data, &inc_dirs, &ninc_dirs);

    /* FIX 4: load content hash database if enabled */
    neo_builddb_t *db = NULL;
    if (g->content_hash) {
        char db_path[PATH_MAX + 16];
        if (g_build_dir[0])
            snprintf(db_path, sizeof(db_path), "%s.neobuilddb", g_build_dir);
        else
            snprintf(db_path, sizeof(db_path), ".neobuilddb");
        db = neo__builddb_load(db_path);
    }

    /* compile sources */
    for (size_t i = 0; i < t->nsources; i++) {
        char *obj = neo__make_output_path(t->sources[i], NULL);
        if (!obj) continue;

        /* Check if recompilation is needed */
        bool needs_build;
        if (db) {
            needs_build = neo__builddb_needs_rebuild(db, t->sources[i],
                full_cflags.data ? full_cflags.data : "", effective_cc);
        } else {
            needs_build = neo__needs_recompile_ex(t->sources[i], obj, inc_dirs, ninc_dirs);
        }

        if (!needs_build) {
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
            neo__free_include_dirs(inc_dirs, ninc_dirs);
            if (db) { neo__builddb_save(db); neo__builddb_free(db); }
            return false;
        }
        NEO_LOGF(NEO_LOG_INFO, "Compiled '%s'", t->sources[i]);

        /* FIX 4: update content hash after successful compilation */
        if (db) {
            neo__builddb_update(db, t->sources[i],
                full_cflags.data ? full_cflags.data : "", effective_cc);
        }

        free(obj);
    }
    neostr_free(&full_cflags);
    free(ccache_prefix);
    neo__free_include_dirs(inc_dirs, ninc_dirs);

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
        if (db) { neo__builddb_save(db); neo__builddb_free(db); }
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

    /* FIX 4: save builddb at end */
    if (db) {
        neo__builddb_save(db);
        neo__builddb_free(db);
    }

    return true;
}

/* ================================================================
 *  FIX 2 & 3: Toposort + in-process sequential build with error propagation
 * ================================================================ */

int neo_graph_build(neo_graph_t *g)
{
    if (!g || g->ntargets == 0) return 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    size_t n = g->ntargets;

    /* compute in-degrees: indeg[i] = number of deps target i has */
    uint32_t *indeg = (uint32_t *)calloc(n, sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        g->targets[i]->state = NEO_TSTATE_PENDING;
        indeg[i] = (uint32_t)g->targets[i]->ndeps;
    }

    /* ready queue: targets with indeg 0 */
    uint32_t *queue = (uint32_t *)malloc(n * sizeof(uint32_t));
    size_t qhead = 0, qtail = 0;
    for (size_t i = 0; i < n; i++)
        if (indeg[i] == 0) { queue[qtail++] = (uint32_t)i; g->targets[i]->state = NEO_TSTATE_READY; }

    int done = 0, failed = 0;

    while (qhead < qtail) {
        uint32_t tid = queue[qhead++];
        neo_target_t *t = g->targets[tid];

        /* FIX 3: skip targets that were already marked FAILED due to dependency failure */
        if (t->state == NEO_TSTATE_FAILED) {
            failed++;
            /* Propagate failure to dependents */
            for (size_t i = 0; i < n; i++) {
                for (size_t j = 0; j < g->targets[i]->ndeps; j++) {
                    if (g->targets[i]->deps[j] == tid) {
                        indeg[i]--;
                        if (g->targets[i]->state != NEO_TSTATE_FAILED) {
                            g->targets[i]->state = NEO_TSTATE_FAILED;
                            NEO_LOGF(NEO_LOG_ERROR, "Target '%s' skipped because dependency '%s' failed",
                                     g->targets[i]->name, t->name);
                        }
                        if (indeg[i] == 0) {
                            queue[qtail++] = (uint32_t)i;
                        }
                    }
                }
            }
            continue;
        }

        if (g_dry_run) {
            NEO_LOGF(NEO_LOG_INFO, "[DRY-RUN] Would build target '%s'", t->name);
            t->state = NEO_TSTATE_DONE;
            done++;
        } else {
            /* FIX 1 & 2: build in-process, no fork */
            t->state = NEO_TSTATE_BUILDING;
            bool success = neo__build_single_target(g, t);

            if (success) {
                t->state = NEO_TSTATE_DONE;
                done++;
            } else {
                t->state = NEO_TSTATE_FAILED;
                failed++;
                NEO_LOGF(NEO_LOG_ERROR, "Target '%s' failed", t->name);

                /* FIX 3: propagate failure to all dependents */
                for (size_t i = 0; i < n; i++) {
                    for (size_t j = 0; j < g->targets[i]->ndeps; j++) {
                        if (g->targets[i]->deps[j] == tid) {
                            if (g->targets[i]->state != NEO_TSTATE_FAILED) {
                                g->targets[i]->state = NEO_TSTATE_FAILED;
                                NEO_LOGF(NEO_LOG_ERROR, "Target '%s' skipped because dependency '%s' failed",
                                         g->targets[i]->name, t->name);
                            }
                        }
                    }
                }
            }
        }

        /* unblock dependents */
        for (size_t i = 0; i < n; i++) {
            for (size_t j = 0; j < g->targets[i]->ndeps; j++) {
                if (g->targets[i]->deps[j] == tid) {
                    indeg[i]--;
                    if (indeg[i] == 0 && g->targets[i]->state == NEO_TSTATE_PENDING) {
                        g->targets[i]->state = NEO_TSTATE_READY;
                        queue[qtail++] = (uint32_t)i;
                    }
                    /* Also enqueue failed targets so they get counted */
                    if (indeg[i] == 0 && g->targets[i]->state == NEO_TSTATE_FAILED) {
                        queue[qtail++] = (uint32_t)i;
                    }
                }
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

    free(indeg); free(queue);
    return done;
}

int neo_graph_build_target(neo_graph_t *g, const char *target_name)
{
    if (!g || !target_name) return -1;
    neo_target_t *t = neo_graph_find(g, target_name);
    if (!t) { NEO_LOGF(NEO_LOG_ERROR, "Target '%s' not found", target_name); return -1; }

    /* mark only this target and its transitive deps */
    bool *needed = (bool *)calloc(g->ntargets, sizeof(bool));
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

    /* mark unneeded targets as DONE so they are skipped */
    for (size_t i = 0; i < g->ntargets; i++) {
        if (!needed[i]) g->targets[i]->state = NEO_TSTATE_DONE;
        else g->targets[i]->state = NEO_TSTATE_PENDING;
    }
    free(needed);

    return neo_graph_build(g);
}

/* ================================================================
 *  Graph Export (DOT format)
 * ================================================================ */

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
