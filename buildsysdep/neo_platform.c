#include "neo_internal.h"

neo_process_t neo__spawn(const char *const argv[])
{
    neo_process_t proc = {-1};
    pid_t p = fork();
    if (p == -1) return proc;
    if (p == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    proc.pid = p;
    return proc;
}

neo_process_t neo__spawn_shell(neoshell_t shell, const char *cmdline)
{
    const char *sh;
    switch (shell) {
    case NEO_BASH: sh = "/bin/bash"; break;
#ifndef NEO_PLATFORM_MACOS
    case NEO_DASH: sh = "/bin/dash"; break;
#endif
    default:       sh = "/bin/sh"; break;
    }
    const char *argv[] = {sh, "-c", cmdline, NULL};
    return neo__spawn(argv);
}

int neo__wait(neo_process_t proc)
{
    if (proc.pid <= 0) return -1;
    int wstatus;
    if (waitpid(proc.pid, &wstatus, 0) == -1) return -1;
    if (WIFEXITED(wstatus)) return WEXITSTATUS(wstatus);
    return -1;
}

neo_process_t neo__wait_any(int *exit_code)
{
    neo_process_t proc = {-1};
    int wstatus;
    pid_t p = waitpid(-1, &wstatus, 0);
    if (p <= 0) return proc;
    proc.pid = p;
    if (exit_code) {
        if (WIFEXITED(wstatus)) *exit_code = WEXITSTATUS(wstatus);
        else if (WIFSIGNALED(wstatus)) *exit_code = -WTERMSIG(wstatus);
        else *exit_code = -1;
    }
    return proc;
}

char *neo__capture_output(const char *const argv[])
{
    int pipefd[2];
    if (pipe(pipefd) == -1) return NULL;

    pid_t p = fork();
    if (p == -1) { close(pipefd[0]); close(pipefd[1]); return NULL; }

    if (p == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);
    neostr_t buf = neostr_empty();
    char tmp[4096];
    ssize_t n;
    while ((n = read(pipefd[0], tmp, sizeof(tmp) - 1)) > 0) {
        tmp[n] = '\0';
        neostr_append(&buf, tmp);
    }
    close(pipefd[0]);

    int wstatus;
    waitpid(p, &wstatus, 0);

    /* trim trailing newline */
    if (buf.len > 0 && buf.data[buf.len - 1] == '\n')
        buf.data[--buf.len] = '\0';

    return neostr_to_cstr(&buf);
}

const char *neo__ar_cmd(void)
{
    return "ar";
}

const char *neo__ranlib_cmd(void)
{
    return "ranlib";
}
