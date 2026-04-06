# Global Settings

These functions configure global defaults that affect all subsequent compilation and linking operations.

---

### `neo_set_global_default_compiler`

> Set the default compiler used when `NEO_GLOBAL_DEFAULT` is passed.

```c
void neo_set_global_default_compiler(neocompiler_t compiler);
```

**Parameters:**
- `compiler` -- one of `NEO_GCC`, `NEO_CLANG`, `NEO_GPP`, `NEO_CLANGPP`, `NEO_LD`, `NEO_AS`.

**Returns:** Nothing.

**Example:**

```c
neo_set_global_default_compiler(NEO_CLANG);
// All calls using NEO_GLOBAL_DEFAULT now invoke clang
neo_compile_to_object_file(NEO_GLOBAL_DEFAULT, "main.c", NULL, NULL, false);
```

---

### `neo_get_global_default_compiler`

> Retrieve the current global default compiler.

```c
neocompiler_t neo_get_global_default_compiler(void);
```

**Parameters:** None.

**Returns:** The current `neocompiler_t` value set as the global default.

**Example:**

```c
neo_set_global_default_compiler(NEO_GCC);
neocompiler_t cc = neo_get_global_default_compiler(); // NEO_GCC
```

---

### `neo_set_verbosity`

> Control how much output neobuild produces.

```c
void neo_set_verbosity(neoverbosity_t v);
```

**Parameters:**
- `v` -- one of:
  - `NEO_QUIET` -- suppress all non-error output.
  - `NEO_NORMAL` -- default output level.
  - `NEO_VERBOSE` -- print every command before execution.

**Returns:** Nothing.

**Example:**

```c
neo_set_verbosity(NEO_VERBOSE);
```

---

### `neo_set_dry_run`

> Enable or disable dry-run mode. When enabled, commands are printed but not executed.

```c
void neo_set_dry_run(bool enabled);
```

**Parameters:**
- `enabled` -- `true` to enable dry-run mode, `false` to disable.

**Returns:** Nothing.

**Example:**

```c
neo_set_dry_run(true);
// Subsequent compile/link calls print commands without running them
neo_compile_to_object_file(NEO_GCC, "main.c", NULL, NULL, false);
```

---

### `neo_set_build_dir`

> Set the output directory for all build artifacts (object files, libraries, executables).

```c
void neo_set_build_dir(const char *dir);
```

**Parameters:**
- `dir` -- path to the build directory. Created automatically if it does not exist.

**Returns:** Nothing.

**Example:**

```c
neo_set_build_dir("build");
// Object files go to build/*.o, executables to build/, etc.
```

---

### `neo_set_profile`

> Set the active build profile. Profiles automatically inject compiler flags.

```c
void neo_set_profile(neoprofile_t profile);
```

**Parameters:**
- `profile` -- one of:
  - `NEO_PROFILE_NONE` -- no automatic flags.
  - `NEO_PROFILE_DEBUG` -- `-g -O0`.
  - `NEO_PROFILE_RELEASE` -- `-O2 -DNDEBUG`.
  - `NEO_PROFILE_RELDBG` -- `-O2 -g`.

**Returns:** Nothing.

**Example:**

```c
neo_set_profile(NEO_PROFILE_RELEASE);
// All compilations now include -O2 -DNDEBUG
```

---

### `neo_set_jobs`

> Set the maximum number of parallel compilation jobs.

```c
void neo_set_jobs(int n);
```

**Parameters:**
- `n` -- maximum number of concurrent compilations. Use the number of CPU cores as a reasonable default.

**Returns:** Nothing.

**Example:**

```c
neo_set_jobs(8);
```
