# Advanced

This page covers the arena allocator, compile_commands.json export, configuration file parsing, self-rebuild, and logging.

---

## Arena Allocator

The arena allocator is a bump allocator for build-time string and memory allocation. All allocations are freed in bulk when the arena is destroyed, eliminating the need to track individual allocations.

### `neo_arena_create`

> Create a new arena with the given page size.

```c
neo_arena_t *neo_arena_create(size_t page_size);
```

**Parameters:**
- `page_size` -- size in bytes for each arena page. Use `4096` as a reasonable default.

**Returns:** Pointer to a new arena, or `NULL` on failure.

**Example:**

```c
neo_arena_t *a = neo_arena_create(4096);
```

---

### `neo_arena_alloc`

> Allocate a block of memory from the arena.

```c
void *neo_arena_alloc(neo_arena_t *a, size_t size);
```

**Parameters:**
- `a` -- the arena.
- `size` -- number of bytes to allocate.

**Returns:** Pointer to the allocated memory. The memory is not initialized.

**Example:**

```c
char *buf = neo_arena_alloc(a, 256);
snprintf(buf, 256, "build/%s.o", name);
```

---

### `neo_arena_strdup`

> Duplicate a string into the arena.

```c
char *neo_arena_strdup(neo_arena_t *a, const char *s);
```

**Parameters:**
- `a` -- the arena.
- `s` -- the string to duplicate.

**Returns:** Pointer to the duplicated string in the arena.

**Example:**

```c
char *name = neo_arena_strdup(a, "myapp");
```

---

### `neo_arena_sprintf`

> Format a string and allocate it in the arena.

```c
char *neo_arena_sprintf(neo_arena_t *a, const char *fmt, ...);
```

**Parameters:**
- `a` -- the arena.
- `fmt` -- printf-style format string.
- `...` -- format arguments.

**Returns:** Pointer to the formatted string in the arena.

**Example:**

```c
char *obj = neo_arena_sprintf(a, "build/%s.o", basename);
char *flag = neo_arena_sprintf(a, "-DVERSION=%d", version);
```

---

### `neo_arena_destroy`

> Destroy the arena and free all memory allocated from it.

```c
void neo_arena_destroy(neo_arena_t *a);
```

**Parameters:**
- `a` -- the arena to destroy.

**Returns:** Nothing.

**Example:**

```c
neo_arena_t *a = neo_arena_create(4096);

char *s1 = neo_arena_strdup(a, "hello");
char *s2 = neo_arena_sprintf(a, "build/%s.o", name);
void *buf = neo_arena_alloc(a, 1024);

// All three allocations freed at once:
neo_arena_destroy(a);
```

---

## compile_commands.json Export

### `neo_export_compile_commands`

> Export a `compile_commands.json` compilation database for IDE/LSP integration (clangd, ccls, etc.).

```c
bool neo_export_compile_commands(const char *output_path);
```

**Parameters:**
- `output_path` -- path to write the JSON file.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neo_export_compile_commands("compile_commands.json");
```

The generated file follows the [JSON Compilation Database](https://clang.llvm.org/docs/JSONCompilationDatabase.html) format used by clangd and other tools.

---

## Configuration Parsing

### `neo_parse_config`

> Parse a key-value configuration file.

```c
neoconfig_t *neo_parse_config(const char *config_file_path, size_t *config_arr_len);
```

**Parameters:**
- `config_file_path` -- path to the configuration file. Format: `key = value`, one per line.
- `config_arr_len` -- output pointer for the number of entries parsed.

**Returns:** Array of `neoconfig_t` entries, or `NULL` on failure.

**Example:**

Given a file `build.conf`:
```
compiler = clang
optimize = true
output_dir = build
```

```c
size_t len;
neoconfig_t *cfg = neo_parse_config("build.conf", &len);
for (size_t i = 0; i < len; i++) {
    printf("%s = %s\n", cfg[i].key, cfg[i].value);
}
neo_free_config(cfg, len);
```

---

### `neo_parse_config_arg`

> Parse key-value pairs from command-line arguments.

```c
neoconfig_t *neo_parse_config_arg(char **argv, size_t *config_arr_len);
```

**Parameters:**
- `argv` -- the program's argument vector. Expected format: `key=value` arguments.
- `config_arr_len` -- output pointer for the number of entries parsed.

**Returns:** Array of `neoconfig_t` entries.

**Example:**

```bash
./strata compiler=clang optimize=true
```

```c
size_t len;
neoconfig_t *cfg = neo_parse_config_arg(argv, &len);
for (size_t i = 0; i < len; i++) {
    if (strcmp(cfg[i].key, "compiler") == 0) {
        // use cfg[i].value
    }
}
```

---

### `neo_free_config`

> Free a configuration array returned by `neo_parse_config` or `neo_parse_config_arg`.

```c
bool neo_free_config(neoconfig_t *config_arr, size_t config_arr_len);
```

**Parameters:**
- `config_arr` -- the configuration array.
- `config_arr_len` -- the number of entries.

**Returns:** `true` on success, `false` on failure.

---

## Self-Rebuild

### `neorebuild`

> Check if the build file has been modified and, if so, recompile and re-execute it.

```c
bool neorebuild(const char *build_file, char **argv, int *argc);
```

**Parameters:**
- `build_file` -- path to the build file (e.g., `"neo.c"`).
- `argv` -- the program's argument vector (passed through on re-execution).
- `argc` -- pointer to the argument count.

**Returns:** `true` if the build file was recompiled and re-executed (in which case this function does not return in the original process). `false` if no rebuild was needed.

Place this call at the very beginning of `main()`:

```c
int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);
    // If neo.c changed, we never reach here -- the new binary runs instead
    // ...
}
```

Pass `--no-rebuild` as a command-line argument to skip the self-rebuild check.

---

## Logging

### `NEO_LOGF`

> Log a formatted message at the specified level.

```c
#define NEO_LOGF(level, fmt, ...) \
    neo__logf((level), (fmt), ##__VA_ARGS__)
```

**Parameters:**
- `level` -- one of `NEO_LOG_ERROR`, `NEO_LOG_WARNING`, `NEO_LOG_INFO`, `NEO_LOG_DEBUG`.
- `fmt` -- printf-style format string.
- `...` -- format arguments.

**Returns:** Nothing.

Log levels:

| Level | Description |
|-------|-------------|
| `NEO_LOG_ERROR` | Errors that prevent the build from continuing |
| `NEO_LOG_WARNING` | Non-fatal issues |
| `NEO_LOG_INFO` | Informational messages |
| `NEO_LOG_DEBUG` | Detailed debug output (only shown in verbose mode) |

**Example:**

```c
NEO_LOGF(NEO_LOG_INFO, "Compiling %s with %s", source, "gcc");
NEO_LOGF(NEO_LOG_ERROR, "Failed to open %s: %s", path, strerror(errno));
NEO_LOGF(NEO_LOG_WARNING, "Package %s not found, disabling feature", pkg_name);
NEO_LOGF(NEO_LOG_DEBUG, "Hash for %s: %lx", file, hash);
```

---

## Filesystem Utilities

### `neo_mkdir`

> Create a directory (with parents) if it does not exist.

```c
bool neo_mkdir(const char *dir_path, mode_t mode);
```

**Parameters:**
- `dir_path` -- path to the directory.
- `mode` -- Unix permissions (e.g., `0755`).

**Returns:** `true` on success (or if the directory already exists), `false` on failure.

**Example:**

```c
neo_mkdir("build/objects", 0755);
```
