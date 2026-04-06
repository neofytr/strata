# Examples

Complete build file examples demonstrating common patterns.

---

## Example 1: Simple Single-File Project

The simplest possible build file. Compiles one source file and links it.

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_compile_to_object_file(NEO_GCC, "main.c", NULL, "-Wall -Wextra", false);
    neo_link(NEO_GCC, "myapp", NULL, false, "main.o");

    return 0;
}
```

Build and run:

```bash
./strata
./myapp
```

---

## Example 2: Multi-Target Project with Dependencies

A project with a static library and an executable that depends on it, using the graph API.

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

    // Static library: libcore
    const char *lib_srcs[] = {"src/parser.c", "src/lexer.c", "src/ast.c"};
    neo_target_t *lib = neo_add_static_lib(g, "libcore", lib_srcs, 3);
    neo_target_add_cflags(lib, "-Wall -Wextra -Wpedantic");
    neo_target_add_include_dir(lib, "include");

    // Executable: myapp (depends on libcore)
    const char *app_srcs[] = {"src/main.c"};
    neo_target_t *app = neo_add_executable(g, "myapp", app_srcs, 1);
    neo_target_add_include_dir(app, "include");
    neo_target_add_ldflags(app, "-lm");
    neo_target_depends_on(app, lib);

    // External package
    neo_package_t json = neo_find_package("jansson");
    if (json.found) {
        neo_target_use_package(app, &json);
        neo_package_free(&json);
    }

    int rc = neo_graph_build(g);

    // IDE support
    neo_export_compile_commands("compile_commands.json");
    neo_graph_export_dot(g, "build_graph.dot");

    neo_graph_destroy(g);
    return rc;
}
```

---

## Example 3: Cross-Compilation for ARM

Cross-compiling for an ARM target using a custom toolchain.

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build-arm");
    neo_set_profile(NEO_PROFILE_RELEASE);
    neo_set_jobs(4);

    // Set up ARM toolchain
    neo_toolchain_t *tc = neo_toolchain_create("arm-linux-gnueabihf-");
    neo_toolchain_set_sysroot(tc, "/opt/arm-sysroot");
    neo_toolchain_set_cc(tc, "arm-linux-gnueabihf-gcc");
    neo_toolchain_set_cxx(tc, "arm-linux-gnueabihf-g++");

    neo_graph_t *g = neo_graph_create();
    neo_graph_set_toolchain(g, tc);

    // Firmware application
    const char *srcs[] = {"src/main.c", "src/hardware.c", "src/drivers.c"};
    neo_target_t *fw = neo_add_executable(g, "firmware", srcs, 3);
    neo_target_add_cflags(fw, "-mcpu=cortex-a7 -mfpu=neon-vfpv4");
    neo_target_add_include_dir(fw, "include");

    int rc = neo_graph_build(g);

    neo_graph_destroy(g);
    neo_toolchain_destroy(tc);
    return rc;
}
```

---

## Example 4: Shared Library with pkg-config, Tests, and Install

A complete library project: builds a versioned shared library, generates a `.pc` file, runs tests, and installs everything.

```c
#include "buildsysdep/neobuild.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build");
    neo_set_profile(NEO_PROFILE_RELEASE);
    neo_set_jobs(8);

    neo_graph_t *g = neo_graph_create();
    neo_graph_enable_content_hash(g);

    // ── Shared library ──────────────────────────────
    const char *lib_srcs[] = {"src/foo.c", "src/bar.c", "src/baz.c"};
    neo_target_t *lib = neo_add_shared_lib(g, "libfoo", lib_srcs, 3);
    neo_target_add_cflags(lib, "-fPIC -Wall -Wextra");
    neo_target_add_include_dir(lib, "include");
    neo_target_set_version(lib, 1, 2, 0);

    // ── Test executable ─────────────────────────────
    const char *test_srcs[] = {"tests/test_foo.c"};
    neo_target_t *test = neo_add_executable(g, "test_foo", test_srcs, 1);
    neo_target_add_include_dir(test, "include");
    neo_target_depends_on(test, lib);

    // ── Build ───────────────────────────────────────
    int rc = neo_graph_build(g);
    if (rc != 0) {
        neo_graph_destroy(g);
        return rc;
    }

    // ── Run tests ───────────────────────────────────
    neo_test_suite_t *suite = neo_test_suite_create("libfoo-tests");
    neo_test_suite_set_timeout(suite, 15);
    neo_test_suite_add(suite, "test_foo", "./build/test_foo");

    neo_test_results_t r = neo_test_suite_run(suite);
    printf("%d/%d passed (%.1f ms)\n", r.passed, r.total, r.elapsed_ms);
    neo_test_suite_destroy(suite);

    // ── Install ─────────────────────────────────────
    neo_install_dirs_t dirs = neo_install_dirs_default("/usr/local");
    neo_install_target(g, "libfoo", &dirs);

    const char *hdrs[] = {"include/foo.h", "include/bar.h"};
    neo_install_headers(hdrs, 2, "foo", &dirs);

    // ── Generate .pc file ───────────────────────────
    neo_generate_pkg_config(
        "build/libfoo.pc",
        "libfoo",
        "1.2.0",
        "The Foo library for doing foo things",
        "-I/usr/local/include",
        "-L/usr/local/lib -lfoo"
    );

    // ── IDE support ─────────────────────────────────
    neo_export_compile_commands("compile_commands.json");

    neo_graph_destroy(g);
    return (r.failed + r.crashed + r.timed_out) > 0 ? 1 : 0;
}
```
