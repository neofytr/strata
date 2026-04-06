# Packages

strata provides functions to detect system packages via `pkg-config`, probe for headers and libraries, apply package flags to targets, and generate `.pc` files for your own libraries.

## Types

```c
typedef struct {
    char *cflags;   // compiler flags from pkg-config
    char *libs;     // linker flags from pkg-config
    char *version;  // package version string
    bool found;     // true if the package was found
} neo_package_t;
```

---

### `neo_find_package`

> Find a system package using `pkg-config`.

```c
neo_package_t neo_find_package(const char *name);
```

**Parameters:**
- `name` -- the `pkg-config` package name (e.g., `"libpng"`, `"openssl"`).

**Returns:** A `neo_package_t` struct. Check the `found` field to determine if the package was detected. If `found` is `true`, the `cflags`, `libs`, and `version` fields are populated.

**Example:**

```c
neo_package_t png = neo_find_package("libpng");
if (png.found) {
    printf("libpng %s found\n", png.version);
    printf("cflags: %s\n", png.cflags);
    printf("libs:   %s\n", png.libs);
    neo_package_free(&png);
} else {
    NEO_LOGF(NEO_LOG_WARNING, "libpng not found");
}
```

---

### `neo_package_free`

> Free the memory allocated inside a `neo_package_t`.

```c
void neo_package_free(neo_package_t *pkg);
```

**Parameters:**
- `pkg` -- pointer to the package struct to free.

**Returns:** Nothing.

**Example:**

```c
neo_package_t pkg = neo_find_package("zlib");
if (pkg.found) {
    // ... use pkg ...
    neo_package_free(&pkg);
}
```

---

### `neo_check_header`

> Check whether a header file is available on the system.

```c
bool neo_check_header(const char *header);
```

**Parameters:**
- `header` -- the header file name (e.g., `"pthread.h"`, `"sys/epoll.h"`).

**Returns:** `true` if the header exists and can be included, `false` otherwise.

**Example:**

```c
if (neo_check_header("pthread.h")) {
    neo_target_add_cflags(app, "-DHAS_PTHREADS");
    neo_target_add_ldflags(app, "-lpthread");
}
```

---

### `neo_check_lib`

> Check whether a library can be linked against.

```c
bool neo_check_lib(const char *lib);
```

**Parameters:**
- `lib` -- the library name without the `lib` prefix or extension (e.g., `"z"` for `-lz`).

**Returns:** `true` if the library was found, `false` otherwise.

**Example:**

```c
if (neo_check_lib("z")) {
    neo_target_add_ldflags(app, "-lz");
}
```

---

### `neo_check_symbol`

> Check whether a specific symbol exists in a library.

```c
bool neo_check_symbol(const char *lib, const char *symbol);
```

**Parameters:**
- `lib` -- the library name (e.g., `"m"` for libm).
- `symbol` -- the symbol name to look for (e.g., `"cosf"`).

**Returns:** `true` if the symbol was found in the library, `false` otherwise.

**Example:**

```c
if (neo_check_symbol("m", "sinf")) {
    neo_target_add_cflags(app, "-DHAS_SINF");
}
```

---

### `neo_target_use_package`

> Apply a package's compiler and linker flags to a target.

```c
void neo_target_use_package(neo_target_t *t, const neo_package_t *pkg);
```

**Parameters:**
- `t` -- the target to configure.
- `pkg` -- a found package (its `cflags` and `libs` are applied to the target).

**Returns:** Nothing.

**Example:**

```c
neo_package_t ssl = neo_find_package("openssl");
if (ssl.found) {
    neo_target_use_package(app, &ssl);
    neo_package_free(&ssl);
}
```

---

### `neo_generate_pkg_config`

> Generate a `.pc` file for your own library, so other projects can find it via `pkg-config`.

```c
bool neo_generate_pkg_config(const char *output_path, const char *name,
    const char *version, const char *description,
    const char *cflags, const char *libs);
```

**Parameters:**
- `output_path` -- path to write the `.pc` file.
- `name` -- package name.
- `version` -- version string.
- `description` -- one-line description.
- `cflags` -- compiler flags to advertise.
- `libs` -- linker flags to advertise.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neo_generate_pkg_config(
    "libfoo.pc",
    "libfoo",
    "1.0.0",
    "The Foo library",
    "-I/usr/local/include",
    "-L/usr/local/lib -lfoo"
);
```
