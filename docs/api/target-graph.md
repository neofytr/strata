# Target Graph

The target graph API is the recommended way to describe builds with multiple targets and inter-target dependencies. strata topologically sorts the graph and builds targets in the correct order, running independent compilations in parallel.

## Types

```c
typedef enum {
    NEO_TARGET_EXECUTABLE,
    NEO_TARGET_STATIC_LIB,
    NEO_TARGET_SHARED_LIB,
    NEO_TARGET_OBJECT,
    NEO_TARGET_CUSTOM,
} neo_target_type_t;
```

---

## Graph Lifecycle

### `neo_graph_create`

> Create a new, empty build graph.

```c
neo_graph_t *neo_graph_create(void);
```

**Parameters:** None.

**Returns:** Pointer to a new `neo_graph_t`, or `NULL` on failure.

**Example:**

```c
neo_graph_t *g = neo_graph_create();
```

---

### `neo_graph_destroy`

> Destroy a build graph and free all associated memory, including all targets.

```c
void neo_graph_destroy(neo_graph_t *g);
```

**Parameters:**
- `g` -- the graph to destroy.

**Returns:** Nothing.

**Example:**

```c
neo_graph_destroy(g);
```

---

## Adding Targets

### `neo_add_executable`

> Add an executable target to the graph.

```c
neo_target_t *neo_add_executable(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
```

**Parameters:**
- `g` -- the build graph.
- `name` -- target name (also used as the output filename).
- `sources` -- array of source file paths.
- `nsources` -- number of source files.

**Returns:** Pointer to the new target.

**Example:**

```c
const char *srcs[] = {"src/main.c", "src/app.c"};
neo_target_t *app = neo_add_executable(g, "myapp", srcs, 2);
```

---

### `neo_add_static_lib`

> Add a static library target to the graph.

```c
neo_target_t *neo_add_static_lib(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
```

**Parameters:**
- `g` -- the build graph.
- `name` -- target name (used to derive the output filename, e.g., `libfoo.a`).
- `sources` -- array of source file paths.
- `nsources` -- number of source files.

**Returns:** Pointer to the new target.

**Example:**

```c
const char *srcs[] = {"src/parser.c", "src/lexer.c"};
neo_target_t *lib = neo_add_static_lib(g, "libparser", srcs, 2);
```

---

### `neo_add_shared_lib`

> Add a shared library target to the graph.

```c
neo_target_t *neo_add_shared_lib(neo_graph_t *g, const char *name,
                                 const char **sources, size_t nsources);
```

**Parameters:**
- `g` -- the build graph.
- `name` -- target name (used to derive the output filename, e.g., `libfoo.so`).
- `sources` -- array of source file paths.
- `nsources` -- number of source files.

**Returns:** Pointer to the new target.

**Example:**

```c
const char *srcs[] = {"src/foo.c", "src/bar.c"};
neo_target_t *lib = neo_add_shared_lib(g, "libfoo", srcs, 2);
neo_target_set_version(lib, 1, 0, 0); // libfoo.so.1.0.0
```

---

### `neo_add_custom`

> Add a custom command target to the graph.

```c
neo_target_t *neo_add_custom(neo_graph_t *g, const char *name,
                             const char *command);
```

**Parameters:**
- `g` -- the build graph.
- `name` -- target name.
- `command` -- the shell command to execute.

**Returns:** Pointer to the new target.

**Example:**

```c
neo_target_t *gen = neo_add_custom(g, "codegen", "./scripts/generate.sh");
neo_target_depends_on(app, gen); // app depends on codegen
```

---

## Configuring Targets

### `neo_target_set_compiler`

> Override the compiler for a specific target.

```c
void neo_target_set_compiler(neo_target_t *t, neocompiler_t compiler);
```

**Parameters:**
- `t` -- the target.
- `compiler` -- the compiler to use.

**Returns:** Nothing.

**Example:**

```c
neo_target_set_compiler(app, NEO_CLANG);
```

---

### `neo_target_add_cflags`

> Add compiler flags to a target. Can be called multiple times; flags accumulate.

```c
void neo_target_add_cflags(neo_target_t *t, const char *flags);
```

**Parameters:**
- `t` -- the target.
- `flags` -- compiler flags string.

**Returns:** Nothing.

**Example:**

```c
neo_target_add_cflags(app, "-Wall -Wextra");
neo_target_add_cflags(app, "-DVERSION=2");
```

---

### `neo_target_add_ldflags`

> Add linker flags to a target. Can be called multiple times; flags accumulate.

```c
void neo_target_add_ldflags(neo_target_t *t, const char *flags);
```

**Parameters:**
- `t` -- the target.
- `flags` -- linker flags string.

**Returns:** Nothing.

**Example:**

```c
neo_target_add_ldflags(app, "-lm -lpthread");
```

---

### `neo_target_add_include_dir`

> Add an include directory to a target (passed as `-I` to the compiler).

```c
void neo_target_add_include_dir(neo_target_t *t, const char *dir);
```

**Parameters:**
- `t` -- the target.
- `dir` -- path to the include directory.

**Returns:** Nothing.

**Example:**

```c
neo_target_add_include_dir(app, "include");
neo_target_add_include_dir(app, "third_party/json");
```

---

### `neo_target_depends_on`

> Declare that one target depends on another. The dependency is built first; if it is a library, it is automatically linked.

```c
void neo_target_depends_on(neo_target_t *target, neo_target_t *dependency);
```

**Parameters:**
- `target` -- the target that has the dependency.
- `dependency` -- the target that must be built first.

**Returns:** Nothing.

**Example:**

```c
neo_target_t *lib = neo_add_static_lib(g, "libcore", lib_srcs, 2);
neo_target_t *app = neo_add_executable(g, "myapp", app_srcs, 1);
neo_target_depends_on(app, lib); // myapp links against libcore
```

---

### `neo_target_set_version`

> Set the version for a shared library target. Produces versioned shared library files (e.g., `libfoo.so.1.2.3`).

```c
void neo_target_set_version(neo_target_t *t, int major, int minor, int patch);
```

**Parameters:**
- `t` -- the target (must be a shared library).
- `major` -- major version number.
- `minor` -- minor version number.
- `patch` -- patch version number.

**Returns:** Nothing.

**Example:**

```c
neo_target_t *lib = neo_add_shared_lib(g, "libfoo", srcs, 2);
neo_target_set_version(lib, 2, 1, 0); // libfoo.so.2.1.0
```

---

## Building

### `neo_graph_build`

> Build all targets in the graph in dependency order.

```c
int neo_graph_build(neo_graph_t *g);
```

**Parameters:**
- `g` -- the build graph.

**Returns:** `0` on success, non-zero on failure.

**Example:**

```c
int rc = neo_graph_build(g);
if (rc != 0) {
    NEO_LOGF(NEO_LOG_ERROR, "Build failed");
    return rc;
}
```

---

### `neo_graph_build_target`

> Build a single target (and its dependencies) by name.

```c
int neo_graph_build_target(neo_graph_t *g, const char *target_name);
```

**Parameters:**
- `g` -- the build graph.
- `target_name` -- name of the target to build.

**Returns:** `0` on success, non-zero on failure.

**Example:**

```c
// Build only the "tests" target and its dependencies
int rc = neo_graph_build_target(g, "tests");
```

---

### `neo_graph_find`

> Find a target in the graph by name.

```c
neo_target_t *neo_graph_find(neo_graph_t *g, const char *name);
```

**Parameters:**
- `g` -- the build graph.
- `name` -- target name to search for.

**Returns:** Pointer to the target if found, `NULL` otherwise.

**Example:**

```c
neo_target_t *t = neo_graph_find(g, "myapp");
if (t) {
    neo_target_add_cflags(t, "-DEXTRA_FLAG");
}
```

---

## Graph Options

### `neo_graph_enable_content_hash`

> Enable content hash-based change detection instead of timestamps.

```c
void neo_graph_enable_content_hash(neo_graph_t *g);
```

**Parameters:**
- `g` -- the build graph.

**Returns:** Nothing.

**Example:**

```c
neo_graph_t *g = neo_graph_create();
neo_graph_enable_content_hash(g);
```

---

### `neo_graph_enable_ccache`

> Enable ccache for all compilations in the graph. Transparently prepends `ccache` to compiler commands.

```c
void neo_graph_enable_ccache(neo_graph_t *g);
```

**Parameters:**
- `g` -- the build graph.

**Returns:** Nothing.

**Example:**

```c
neo_graph_enable_ccache(g);
```

---

### `neo_graph_export_dot`

> Export the build graph as a Graphviz DOT file for visualization.

```c
bool neo_graph_export_dot(neo_graph_t *g, const char *output_path);
```

**Parameters:**
- `g` -- the build graph.
- `output_path` -- path to the output `.dot` file.

**Returns:** `true` on success, `false` on failure.

**Example:**

```c
neo_graph_export_dot(g, "build_graph.dot");
// Render with: dot -Tpng build_graph.dot -o build_graph.png
```
