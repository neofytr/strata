# Install

These functions handle installing built artifacts (executables, libraries, headers) to standard filesystem locations.

## Types

```c
typedef struct {
    char prefix[512];       // e.g., "/usr/local"
    char bindir[512];       // e.g., "/usr/local/bin"
    char libdir[512];       // e.g., "/usr/local/lib"
    char includedir[512];   // e.g., "/usr/local/include"
    char pkgconfigdir[512]; // e.g., "/usr/local/lib/pkgconfig"
} neo_install_dirs_t;
```

---

### `neo_install_dirs_default`

> Create a default install directory layout from a prefix path.

```c
neo_install_dirs_t neo_install_dirs_default(const char *prefix);
```

**Parameters:**
- `prefix` -- the installation prefix (e.g., `"/usr/local"`, `"/opt/myapp"`).

**Returns:** A `neo_install_dirs_t` with all paths derived from the prefix:
- `bindir` = `prefix/bin`
- `libdir` = `prefix/lib`
- `includedir` = `prefix/include`
- `pkgconfigdir` = `prefix/lib/pkgconfig`

**Example:**

```c
neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");
// dirs.prefix       -> "/usr/local"
// dirs.bindir       -> "/usr/local/bin"
// dirs.libdir       -> "/usr/local/lib"
// dirs.includedir   -> "/usr/local/include"
// dirs.pkgconfigdir -> "/usr/local/lib/pkgconfig"
```

---

### `neo_install_target`

> Install a built target (executable or library) to the appropriate directory.

```c
bool neo_install_target(neo_graph_t *g, const char *target_name,
                        const neo_install_dirs_t *dirs);
```

**Parameters:**
- `g` -- the build graph containing the target.
- `target_name` -- the name of the target to install.
- `dirs` -- the install directory layout.

**Returns:** `true` on success, `false` on failure.

Executables are installed to `bindir`, static libraries to `libdir`, and shared libraries to `libdir` (with versioned symlinks if a version was set).

**Example:**

```c
neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");

// Install the executable
neo_install_target(g, "myapp", &dirs);

// Install the library
neo_install_target(g, "libcore", &dirs);
```

---

### `neo_install_headers`

> Install header files to the include directory, optionally under a subdirectory.

```c
bool neo_install_headers(const char **headers, size_t nheaders,
                         const char *subdir, const neo_install_dirs_t *dirs);
```

**Parameters:**
- `headers` -- array of header file paths to install.
- `nheaders` -- number of headers.
- `subdir` -- subdirectory under `includedir` to install into (e.g., `"foo"` installs to `includedir/foo/`). Pass `NULL` to install directly into `includedir`.
- `dirs` -- the install directory layout.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");

const char *hdrs[] = {"include/core.h", "include/util.h"};
neo_install_headers(hdrs, 2, "mylib", &dirs);
// Installs to: /usr/local/include/mylib/core.h
//              /usr/local/include/mylib/util.h
```

---

## Complete Install Example

```c
neo_graph_t *g = neo_graph_create();

// ... add targets and build ...

neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");

// Install executable
neo_install_target(g, "myapp", &dirs);

// Install library
neo_install_target(g, "libcore", &dirs);

// Install headers
const char *hdrs[] = {"include/core.h", "include/types.h"};
neo_install_headers(hdrs, 2, "core", &dirs);

// Generate and install pkg-config file
neo_generate_pkg_config(
    "build/libcore.pc", "libcore", "1.0.0",
    "Core library", "-I/usr/local/include", "-L/usr/local/lib -lcore"
);

neo_graph_destroy(g);
```
