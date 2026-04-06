#include "neo_internal.h"

/* ================================================================
 *  Filesystem
 * ================================================================ */

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

/* ================================================================
 *  Self-Rebuild
 * ================================================================ */

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

/* ================================================================
 *  Configuration Parsing
 * ================================================================ */

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
