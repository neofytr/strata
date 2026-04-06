#include "neo_internal.h"

/* ================================================================
 *  Package / Feature Detection
 * ================================================================ */

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
    char tmpbase[] = "/tmp/neo_probe_XXXXXX";
    int fd = mkstemp(tmpbase);
    if (fd < 0) return false;
    close(fd);

    char tmpfile[PATH_MAX];
    snprintf(tmpfile, sizeof(tmpfile), "%s.c", tmpbase);
    rename(tmpbase, tmpfile);

    FILE *f = fopen(tmpfile, "w");
    if (!f) { unlink(tmpfile); return false; }
    fputs(code, f);
    fclose(f);

    char obj[PATH_MAX];
    snprintf(obj, sizeof(obj), "%s.o", tmpbase);

    const char *argv[] = {"cc", "-c", tmpfile, "-o", obj, "-w", NULL};
    neo_process_t proc = neo__spawn(argv);
    int ret = neo__wait(proc);

    unlink(tmpfile);
    unlink(obj);
    return ret == 0;
}

static bool neo__try_link(const char *code, const char *lib_flag)
{
    char tmpbase[] = "/tmp/neo_probe_XXXXXX";
    int fd = mkstemp(tmpbase);
    if (fd < 0) return false;
    close(fd);

    char tmpfile[PATH_MAX];
    snprintf(tmpfile, sizeof(tmpfile), "%s.c", tmpbase);
    rename(tmpbase, tmpfile);

    FILE *f = fopen(tmpfile, "w");
    if (!f) { unlink(tmpfile); return false; }
    fputs(code, f);
    fclose(f);

    char outfile[PATH_MAX];
    snprintf(outfile, sizeof(outfile), "%s", tmpbase);

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
