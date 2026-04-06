# Toolchains

Toolchains enable cross-compilation by configuring a compiler prefix, sysroot, and explicit compiler paths. A toolchain can be applied to the entire build graph or to individual targets.

---

### `neo_toolchain_create`

> Create a new toolchain with a given prefix.

```c
neo_toolchain_t *neo_toolchain_create(const char *prefix);
```

**Parameters:**
- `prefix` -- the toolchain prefix (e.g., `"arm-linux-gnueabihf-"`). This prefix is prepended to compiler names.

**Returns:** Pointer to a new `neo_toolchain_t`, or `NULL` on failure.

**Example:**

```c
neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
```

---

### `neo_toolchain_set_sysroot`

> Set the sysroot directory for the toolchain. Passed as `--sysroot=` to the compiler.

```c
void neo_toolchain_set_sysroot(neo_toolchain_t *tc, const char *sysroot);
```

**Parameters:**
- `tc` -- the toolchain.
- `sysroot` -- path to the sysroot directory.

**Returns:** Nothing.

**Example:**

```c
neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
```

---

### `neo_toolchain_set_cc`

> Override the C compiler path for the toolchain.

```c
void neo_toolchain_set_cc(neo_toolchain_t *tc, const char *cc);
```

**Parameters:**
- `tc` -- the toolchain.
- `cc` -- path or name of the C compiler.

**Returns:** Nothing.

**Example:**

```c
neo_toolchain_set_cc(tc, "arm-linux-gnueabihf-gcc");
```

---

### `neo_toolchain_set_cxx`

> Override the C++ compiler path for the toolchain.

```c
void neo_toolchain_set_cxx(neo_toolchain_t *tc, const char *cxx);
```

**Parameters:**
- `tc` -- the toolchain.
- `cxx` -- path or name of the C++ compiler.

**Returns:** Nothing.

**Example:**

```c
neo_toolchain_set_cxx(tc, "arm-linux-gnueabihf-g++");
```

---

### `neo_toolchain_destroy`

> Destroy a toolchain and free its memory.

```c
void neo_toolchain_destroy(neo_toolchain_t *tc);
```

**Parameters:**
- `tc` -- the toolchain to destroy.

**Returns:** Nothing.

**Example:**

```c
neo_toolchain_destroy(tc);
```

---

### `neo_graph_set_toolchain`

> Apply a toolchain to every target in the build graph.

```c
void neo_graph_set_toolchain(neo_graph_t *g, neo_toolchain_t *tc);
```

**Parameters:**
- `g` -- the build graph.
- `tc` -- the toolchain to apply.

**Returns:** Nothing.

**Example:**

```c
neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
neo_toolchain_set_cc(tc, "arm-linux-gnueabihf-gcc");

neo_graph_set_toolchain(g, tc); // all targets use this toolchain
```

---

### `neo_target_set_toolchain`

> Apply a toolchain to a single target, overriding the graph-level toolchain.

```c
void neo_target_set_toolchain(neo_target_t *t, neo_toolchain_t *tc);
```

**Parameters:**
- `t` -- the target.
- `tc` -- the toolchain to apply.

**Returns:** Nothing.

**Example:**

```c
// Most targets use the ARM toolchain, but host tools use the native compiler
neo_graph_set_toolchain(g, arm_tc);
neo_target_set_toolchain(host_tool, NULL); // override: use native compiler
```

---

## Full Cross-Compilation Example

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build-arm");
    neo_set_profile(NEO_PROFILE_RELEASE);

    neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
    neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
    neo_toolchain_set_cc(tc, "arm-linux-gnueabihf-gcc");
    neo_toolchain_set_cxx(tc, "arm-linux-gnueabihf-g++");

    neo_graph_t *g = neo_graph_create();
    neo_graph_set_toolchain(g, tc);

    const char *srcs[] = {"src/main.c", "src/hw.c"};
    neo_target_t *app = neo_add_executable(g, "firmware", srcs, 2);
    neo_target_add_cflags(app, "-mcpu=cortex-a7");

    int rc = neo_graph_build(g);

    neo_graph_destroy(g);
    neo_toolchain_destroy(tc);
    return rc;
}
```
