#include "neo_internal.h"

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

/* ================================================================
 *  Process Execution
 * ================================================================ */

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
