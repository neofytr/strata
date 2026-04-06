# Command API

The command API lets you build and execute arbitrary shell commands. Commands can run through a shell interpreter (`bash`, `dash`, `sh`) or directly via `execvp` (`NEO_DIRECT`).

## Shell Types

| Value | Description |
|-------|-------------|
| `NEO_DASH` | Run via `/bin/dash` |
| `NEO_BASH` | Run via `/bin/bash` |
| `NEO_SH` | Run via `/bin/sh` |
| `NEO_DIRECT` | Direct `execvp`, no shell involved |

---

### `neocmd_create`

> Create a new command builder with the specified shell type.

```c
neocmd_t *neocmd_create(neoshell_t shell);
```

**Parameters:**
- `shell` -- the shell to use for execution (`NEO_BASH`, `NEO_DASH`, `NEO_SH`, or `NEO_DIRECT`).

**Returns:** Pointer to a newly allocated `neocmd_t`, or `NULL` on failure.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_BASH);
```

---

### `neocmd_delete`

> Free a command and all its associated memory.

```c
bool neocmd_delete(neocmd_t *neocmd);
```

**Parameters:**
- `neocmd` -- the command to free.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_DIRECT);
// ... use cmd ...
neocmd_delete(cmd);
```

---

### `neocmd_append`

> Append one or more arguments to the command. This is a variadic macro that automatically terminates the argument list with `NULL`.

```c
#define neocmd_append(neocmd_ptr, ...) \
    neocmd_append_null((neocmd_ptr), __VA_ARGS__, NULL)
```

**Parameters:**
- `neocmd_ptr` -- pointer to the command.
- `...` -- one or more string arguments.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_DIRECT);
neocmd_append(cmd, "gcc", "-o", "hello", "hello.c");
```

---

### `neocmd_append_null`

> Append arguments to the command, with an explicit `NULL` terminator. This is the underlying function used by `neocmd_append`.

```c
bool neocmd_append_null(neocmd_t *neocmd, ...);
```

**Parameters:**
- `neocmd` -- pointer to the command.
- `...` -- string arguments, terminated by `NULL`.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neocmd_append_null(cmd, "echo", "hello", NULL);
```

---

### `neocmd_run_sync`

> Execute the command synchronously and wait for it to finish.

```c
bool neocmd_run_sync(neocmd_t *neocmd, int *status, int *code, bool print_status_desc);
```

**Parameters:**
- `neocmd` -- the command to execute.
- `status` -- output pointer for the raw wait status (can be `NULL`).
- `code` -- output pointer for the exit code (can be `NULL`).
- `print_status_desc` -- if `true`, print a description of the exit status.

**Returns:** `true` if the command was launched and waited on successfully, `false` on failure.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_BASH);
neocmd_append(cmd, "echo", "hello world");

int status, code;
neocmd_run_sync(cmd, &status, &code, true);
printf("Exit code: %d\n", code);
neocmd_delete(cmd);
```

---

### `neocmd_run_async`

> Launch the command asynchronously and return immediately.

```c
pid_t neocmd_run_async(neocmd_t *neocmd);
```

**Parameters:**
- `neocmd` -- the command to launch.

**Returns:** The PID of the child process, or `-1` on failure.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_DIRECT);
neocmd_append(cmd, "make", "-j4");

pid_t pid = neocmd_run_async(cmd);
// ... do other work ...
int status, code;
neoshell_wait(pid, &status, &code, true);
neocmd_delete(cmd);
```

---

### `neocmd_render`

> Render the complete command as a single string. Useful for logging or debugging.

```c
const char *neocmd_render(neocmd_t *neocmd);
```

**Parameters:**
- `neocmd` -- the command to render.

**Returns:** A string representation of the command. The string is owned by the command and freed when `neocmd_delete` is called.

**Example:**

```c
neocmd_t *cmd = neocmd_create(NEO_DIRECT);
neocmd_append(cmd, "gcc", "-o", "hello", "hello.c");
printf("Running: %s\n", neocmd_render(cmd));
```

---

### `neoshell_wait`

> Wait for an asynchronous process to finish.

```c
bool neoshell_wait(pid_t pid, int *status, int *code, bool should_print);
```

**Parameters:**
- `pid` -- the process ID returned by `neocmd_run_async`.
- `status` -- output pointer for the raw wait status (can be `NULL`).
- `code` -- output pointer for the exit code (can be `NULL`).
- `should_print` -- if `true`, print a description of the exit status.

**Returns:** `true` if the wait completed successfully, `false` on failure.

**Example:**

```c
pid_t pid = neocmd_run_async(cmd);
int status, code;
neoshell_wait(pid, &status, &code, false);
if (code != 0) {
    NEO_LOGF(NEO_LOG_ERROR, "Command failed with code %d", code);
}
```
