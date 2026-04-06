#include "neo_internal.h"

/* ================================================================
 *  FNV-1a Hash (for content hashing)
 * ================================================================ */

uint64_t neo__fnv1a(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t neo__fnv1a_str(const char *s)
{
    return s ? neo__fnv1a(s, strlen(s)) : 0;
}

uint64_t neo__hash_file(const char *path)
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

/* ================================================================
 *  Header Dependency Tracking (FIX: include_dirs support)
 * ================================================================ */

bool neo__scan_includes(const char *filepath, time_t output_mtime,
                        const char **visited, size_t *visited_count, bool *newer,
                        const char **include_dirs, size_t ninclude_dirs)
{
    if (*newer) return true;
    char real[PATH_MAX];
    if (!realpath(filepath, real)) {
        /* Try include dirs if relative resolution fails */
        if (include_dirs && filepath[0] != '/') {
            for (size_t d = 0; d < ninclude_dirs; d++) {
                char try_path[PATH_MAX];
                snprintf(try_path, sizeof(try_path), "%s/%s", include_dirs[d], filepath);
                if (realpath(try_path, real)) goto resolved;
            }
        }
        return false;
    }
resolved:
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
        neo__scan_includes(inc_path, output_mtime, visited, visited_count, newer,
                           include_dirs, ninclude_dirs);
        if (*newer) break;
    }
    fclose(f);
    return true;
}

bool neo_needs_recompile(const char *source, const char *output)
{
    return neo__needs_recompile_ex(source, output, NULL, 0);
}

bool neo__needs_recompile_ex(const char *source, const char *output,
                             const char **include_dirs, size_t ninclude_dirs)
{
    if (!source || !output) return true;
    struct stat output_stat;
    if (stat(output, &output_stat) == -1) return true;
    const char *visited[NEO_MAX_DEPS] = {0};
    size_t visited_count = 0;
    bool newer = false;
    neo__scan_includes(source, output_stat.st_mtime, visited, &visited_count, &newer,
                       include_dirs, ninclude_dirs);
    for (size_t i = 0; i < visited_count; i++) free((void *)visited[i]);
    return newer;
}

/* ================================================================
 *  Content Hash Database
 * ================================================================ */

neo_builddb_t *neo__builddb_load(const char *path)
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

bool neo__builddb_save(neo_builddb_t *db)
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

bool neo__builddb_needs_rebuild(neo_builddb_t *db, const char *source,
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

void neo__builddb_update(neo_builddb_t *db, const char *source,
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

void neo__builddb_free(neo_builddb_t *db)
{
    if (!db) return;
    free(db->entries);
    free(db);
}
