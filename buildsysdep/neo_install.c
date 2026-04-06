#include "neo_internal.h"

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
    const char *bname = strrchr(t->name, '/');
    bname = bname ? bname + 1 : t->name;
    snprintf(dest, sizeof(dest), "%s/%s", dest_dir, bname);

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
