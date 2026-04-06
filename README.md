# neobuild

**Write your build logic in C. No DSL. No YAML. No Makefiles.**

<!-- badges -->
<!-- [![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt) -->

## Overview

neobuild is a build system where your build file is a plain C program. You `#include` a single header, call the API to describe compilations, link steps, and dependency graphs, and the build system handles incremental rebuilds, parallel compilation, header dependency tracking, and more.

The key idea: if you already know C, you already know how to use neobuild. No new language to learn. Full control via real code -- conditionals, loops, platform checks -- all with zero magic.

## Installation

```bash
git clone https://github.com/neofytr/neobuild.git
cd neobuild
```

The repository contains `strata`, a pre-built bootstrapper binary. You write a C file (e.g. `neo.c`) that includes the neobuild header, then run `./strata` to compile and execute it.

## Quick Start

Create `neo.c`:

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_compile_to_object_file(NEO_GCC, "hello.c", NULL, NULL, false);
    neo_link(NEO_GCC, "hello", NULL, false, "hello.o");

    return 0;
}
```

Build and run:

```bash
./strata       # compiles neo.c, then executes it
./hello        # run your program
```

On subsequent runs, `strata` detects changes to `neo.c` and recompiles the build file automatically before executing it.

## Core Concepts

**Build graph** -- Create a `neo_graph_t`, add targets (executables, libraries, custom commands), declare dependencies between them, then call `neo_graph_build()`. neobuild topologically sorts the graph and builds in parallel.

**Targets** -- Each target has a type (`NEO_TARGET_EXECUTABLE`, `NEO_TARGET_STATIC_LIB`, `NEO_TARGET_SHARED_LIB`, `NEO_TARGET_OBJECT`, `NEO_TARGET_CUSTOM`), a set of source files, compiler/linker flags, and optional dependencies on other targets.

**Incremental rebuilds** -- neobuild tracks header dependencies via the compiler's `-MMD` flag and skips recompilation when sources and headers are unchanged. Enable `neo_graph_enable_content_hash()` for hash-based (rather than timestamp-based) change detection.

**Build profiles** -- `NEO_PROFILE_DEBUG`, `NEO_PROFILE_RELEASE`, `NEO_PROFILE_RELDBG` automatically inject appropriate optimization and debug flags.

## API Reference

### Global Settings

```c
neo_set_global_default_compiler(NEO_CLANG);  // NEO_GCC, NEO_CLANG, NEO_GPP, NEO_CLANGPP
neo_set_verbosity(NEO_VERBOSE);              // NEO_QUIET, NEO_NORMAL, NEO_VERBOSE
neo_set_dry_run(true);                       // print commands without executing
neo_set_build_dir("build");                  // output directory for objects/artifacts
neo_set_profile(NEO_PROFILE_RELEASE);        // inject -O2, strip debug info, etc.
neo_set_jobs(4);                             // max parallel compilation jobs
```

### Command API

Run arbitrary commands through a shell or via direct `execvp`:

```c
neocmd_t *cmd = neocmd_create(NEO_BASH);     // NEO_DASH, NEO_BASH, NEO_SH, NEO_DIRECT
neocmd_append(cmd, "echo", "hello world");

// synchronous
int status, code;
neocmd_run_sync(cmd, &status, &code, true);

// or asynchronous
pid_t pid = neocmd_run_async(cmd);
// ... do other work ...
neoshell_wait(pid, &status, &code, true);

neocmd_delete(cmd);
```

`neocmd_render()` returns the full command string (useful for logging).

### Compilation

**Single file:**

```c
// compiler, source, output (NULL = auto), cflags, force
neo_compile_to_object_file(NEO_GCC, "main.c", NULL, "-Wall -Wextra", false);
```

**Parallel compilation:**

```c
const char *srcs[] = {"a.c", "b.c", "c.c"};
neo_compile_parallel(NEO_GCC, srcs, 3, "-O2", false);
```

**Linking:**

```c
// variadic list of object files, terminated internally by NULL
neo_link(NEO_GCC, "myapp", "-lm -lpthread", false, "a.o", "b.o", "c.o");
```

**Header dependency tracking:**

```c
if (neo_needs_recompile("main.c", "main.o")) {
    // source or any included header changed
}
```

### Target Graph API

For larger projects, use the graph API instead of bare compile/link calls:

```c
neo_graph_t *g = neo_graph_create();

const char *srcs[] = {"main.c", "util.c"};
neo_target_t *app = neo_add_executable(g, "myapp", srcs, 2);
neo_target_set_compiler(app, NEO_CLANG);
neo_target_add_cflags(app, "-Wall -O2");
neo_target_add_ldflags(app, "-lm");
neo_target_add_include_dir(app, "include");

// build everything (returns 0 on success)
int rc = neo_graph_build(g);

// or build a single target by name
int rc2 = neo_graph_build_target(g, "myapp");

neo_graph_destroy(g);
```

**Dependencies between targets:**

```c
const char *lib_srcs[] = {"parser.c", "lexer.c"};
neo_target_t *lib = neo_add_static_lib(g, "libparser", lib_srcs, 2);

neo_target_depends_on(app, lib);  // app links against libparser
```

**Custom targets:**

```c
neo_target_t *gen = neo_add_custom(g, "codegen", "./generate_code.sh");
neo_target_depends_on(app, gen);  // app depends on codegen running first
```

### Library Building

**Static library:**

```c
const char *srcs[] = {"vec.c", "mat.c"};
neo_build_static_lib(NEO_GCC, "libmath", srcs, 2, "-O2");
```

**Shared library:**

```c
neo_build_shared_lib(NEO_GCC, "libmath", srcs, 2, "-O2 -fPIC", "-shared");
```

**Versioned shared library (via graph API):**

```c
neo_target_t *lib = neo_add_shared_lib(g, "libfoo", srcs, 2);
neo_target_set_version(lib, 1, 2, 3);  // produces libfoo.so.1.2.3
```

### Package Detection

Uses `pkg-config` under the hood:

```c
neo_package_t pkg = neo_find_package("libpng");
if (pkg.found) {
    printf("version: %s\n", pkg.version);
    printf("cflags:  %s\n", pkg.cflags);
    printf("libs:    %s\n", pkg.libs);

    neo_target_use_package(app, &pkg);  // apply cflags/libs to target
    neo_package_free(&pkg);
}
```

**Probing headers and libraries:**

```c
if (neo_check_header("pthread.h"))          { /* header exists */ }
if (neo_check_lib("z"))                      { /* -lz links */ }
if (neo_check_symbol("m", "cosf"))           { /* cosf found in libm */ }
```

**Generate a .pc file for your own library:**

```c
neo_generate_pkg_config("libfoo.pc", "libfoo", "1.0.0",
    "Foo library", "-I/usr/local/include", "-L/usr/local/lib -lfoo");
```

### Toolchains and Cross-Compilation

```c
neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
neo_toolchain_set_cc(tc, "arm-linux-gnueabihf-gcc");
neo_toolchain_set_cxx(tc, "arm-linux-gnueabihf-g++");

neo_graph_set_toolchain(g, tc);               // apply to entire graph
// or per-target:
neo_target_set_toolchain(app, tc);

neo_toolchain_destroy(tc);
```

### Install

```c
neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");
// dirs.prefix      = "/usr/local"
// dirs.bindir      = "/usr/local/bin"
// dirs.libdir      = "/usr/local/lib"
// dirs.includedir  = "/usr/local/include"
// dirs.pkgconfigdir = "/usr/local/lib/pkgconfig"

neo_install_target(g, "myapp", &dirs);

const char *hdrs[] = {"include/foo.h", "include/bar.h"};
neo_install_headers(hdrs, 2, "foo", &dirs);   // -> /usr/local/include/foo/
```

### Test Runner

```c
neo_test_suite_t *suite = neo_test_suite_create("unit-tests");
neo_test_suite_set_timeout(suite, 10);  // seconds per test

neo_test_suite_add(suite, "test_pass",  "./tests/test_pass");
neo_test_suite_add(suite, "test_fail",  "./tests/test_fail");
neo_test_suite_add(suite, "test_crash", "./tests/test_crash");

neo_test_results_t r = neo_test_suite_run(suite);
printf("%d/%d passed (%.1f ms)\n", r.passed, r.total, r.elapsed_ms);

neo_test_suite_destroy(suite);
```

### Advanced Features

**Content hashing** -- Use file content hashes instead of timestamps for change detection:

```c
neo_graph_enable_content_hash(g);
```

**ccache** -- Transparently prepend `ccache` to compile commands:

```c
neo_graph_enable_ccache(g);
```

**DOT graph export** -- Visualize the build graph with Graphviz:

```c
neo_graph_export_dot(g, "build_graph.dot");
// then: dot -Tpng build_graph.dot -o build_graph.png
```

**compile_commands.json** -- Generate a compilation database for clangd/LSP:

```c
neo_export_compile_commands("compile_commands.json");
```

### Arena Allocator

A bump allocator for build-time string allocation. Memory is freed in bulk when the arena is destroyed:

```c
neo_arena_t *a = neo_arena_create(4096);

char *s = neo_arena_strdup(a, "hello");
char *f = neo_arena_sprintf(a, "build/%s.o", name);
void *buf = neo_arena_alloc(a, 256);

neo_arena_destroy(a);  // frees everything at once
```

### Configuration Files

Parse key-value configuration from files or command-line arguments:

```c
// from a file (format: key = value, one per line)
size_t len;
neoconfig_t *cfg = neo_parse_config("build.conf", &len);
for (size_t i = 0; i < len; i++)
    printf("%s = %s\n", cfg[i].key, cfg[i].value);
neo_free_config(cfg, len);

// from argv (format: key=value passed as arguments)
neoconfig_t *acfg = neo_parse_config_arg(argv, &len);
```

### Self-Rebuild

Place this at the top of your `main()`. If the build file (`neo.c`) has been modified, neobuild recompiles and re-executes it automatically:

```c
int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);
    // ... rest of build logic ...
}
```

Pass `--no-rebuild` to skip the self-rebuild check.

### Logging

```c
NEO_LOGF(NEO_LOG_INFO,  "compiling %s", filename);
NEO_LOGF(NEO_LOG_ERROR, "missing source: %s", path);
// levels: NEO_LOG_ERROR, NEO_LOG_WARNING, NEO_LOG_INFO, NEO_LOG_DEBUG
```

## Full Example

A project with a static library and an executable that depends on it:

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build");
    neo_set_profile(NEO_PROFILE_RELEASE);
    neo_set_jobs(8);

    neo_graph_t *g = neo_graph_create();
    neo_graph_enable_content_hash(g);
    neo_graph_enable_ccache(g);

    // static library
    const char *lib_srcs[] = {"src/parser.c", "src/lexer.c"};
    neo_target_t *lib = neo_add_static_lib(g, "libcore", lib_srcs, 2);
    neo_target_add_cflags(lib, "-Wall -Wextra");
    neo_target_add_include_dir(lib, "include");

    // executable
    const char *app_srcs[] = {"src/main.c"};
    neo_target_t *app = neo_add_executable(g, "myapp", app_srcs, 1);
    neo_target_add_include_dir(app, "include");
    neo_target_depends_on(app, lib);

    // external package
    neo_package_t png = neo_find_package("libpng");
    if (png.found) {
        neo_target_use_package(app, &png);
        neo_package_free(&png);
    }

    int rc = neo_graph_build(g);

    // export for IDE support
    neo_export_compile_commands("compile_commands.json");
    neo_graph_export_dot(g, "build_graph.dot");

    // install
    neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");
    neo_install_target(g, "myapp", &dirs);

    const char *hdrs[] = {"include/core.h"};
    neo_install_headers(hdrs, 1, "core", &dirs);

    neo_graph_destroy(g);
    return rc;
}
```

## Platform Support

| Platform | Status |
|----------|--------|
| Linux    | Full support |
| macOS    | Partial (POSIX layer works, some features untested) |
| Windows  | Not yet supported |

## License

MIT -- see [LICENSE.txt](LICENSE.txt).
