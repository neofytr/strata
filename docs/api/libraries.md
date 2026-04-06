# Libraries

These functions build static and shared libraries outside the graph API. For graph-based library targets, see [Target Graph](api/target-graph.md).

---

### `neo_build_static_lib`

> Compile source files and archive them into a static library (`.a`).

```c
bool neo_build_static_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags);
```

**Parameters:**
- `compiler` -- the compiler to use.
- `name` -- library name (e.g., `"libmath"`). The `.a` extension is added automatically.
- `sources` -- array of source file paths.
- `nsources` -- number of source files.
- `cflags` -- compiler flags, or `NULL`.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
const char *srcs[] = {"src/vec.c", "src/mat.c"};
neo_build_static_lib(NEO_GCC, "libmath", srcs, 2, "-O2 -Wall");
```

---

### `neo_build_shared_lib`

> Compile source files and link them into a shared library (`.so`).

```c
bool neo_build_shared_lib(neocompiler_t compiler, const char *name,
    const char **sources, size_t nsources, const char *cflags,
    const char *ldflags);
```

**Parameters:**
- `compiler` -- the compiler to use.
- `name` -- library name (e.g., `"libmath"`). The `.so` extension is added automatically.
- `sources` -- array of source file paths.
- `nsources` -- number of source files.
- `cflags` -- compiler flags (typically include `-fPIC`), or `NULL`.
- `ldflags` -- linker flags (typically include `-shared`), or `NULL`.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
const char *srcs[] = {"src/vec.c", "src/mat.c"};
neo_build_shared_lib(NEO_GCC, "libmath", srcs, 2, "-O2 -fPIC", "-shared");
```

---

## Versioned Shared Libraries

To produce versioned shared libraries (e.g., `libfoo.so.1.2.3` with symlinks), use the graph API:

```c
neo_graph_t *g = neo_graph_create();

const char *srcs[] = {"src/foo.c"};
neo_target_t *lib = neo_add_shared_lib(g, "libfoo", srcs, 1);
neo_target_add_cflags(lib, "-fPIC");
neo_target_set_version(lib, 1, 2, 3);

neo_graph_build(g);
// Produces: libfoo.so.1.2.3, libfoo.so.1, libfoo.so (symlinks)

neo_graph_destroy(g);
```

See [`neo_target_set_version`](api/target-graph.md#neo_target_set_version) for details.
