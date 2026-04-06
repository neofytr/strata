# Getting Started

## Prerequisites

- A C compiler (GCC or Clang)
- Linux or macOS (Windows is not yet supported)
- `pkg-config` (optional, for package detection)

## Installation

Clone the repository:

```bash
git clone https://github.com/neofytr/strata.git
cd strata
```

The repository ships with `strata`, a pre-built bootstrapper. No separate installation step is needed -- `strata` compiles your build file and runs it.

## The Bootstrapper: strata

`strata` is the entry point. It looks for a file called `neo.c` in the current directory, compiles it against the strata library, and executes the resulting binary. On subsequent runs, it detects changes to `neo.c` and recompiles automatically.

```bash
./strata           # compile and run neo.c
./strata --help    # show options
```

You can also pass configuration arguments that your build file can read via `neo_parse_config_arg()`.

## Your First Build File

Create a file called `neo.c` in your project root:

```c
#include "buildsysdep/neobuild.h"

int main(int argc, char **argv)
{
    // Self-rebuild: recompiles neo.c if it changed
    neorebuild("neo.c", argv, &argc);

    // Compile hello.c to hello.o
    neo_compile_to_object_file(NEO_GCC, "hello.c", NULL, NULL, false);

    // Link hello.o into the "hello" executable
    neo_link(NEO_GCC, "hello", NULL, false, "hello.o");

    return 0;
}
```

Create `hello.c`:

```c
#include <stdio.h>

int main(void)
{
    printf("Hello from strata!\n");
    return 0;
}
```

Build and run:

```bash
./strata
./hello
```

## Adding Compiler Flags

Pass flags as strings:

```c
neo_compile_to_object_file(NEO_GCC, "hello.c", NULL, "-Wall -Wextra -O2", false);
neo_link(NEO_GCC, "hello", "-lm", false, "hello.o");
```

## Using Global Settings

Configure defaults that apply to all compilations:

```c
int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_global_default_compiler(NEO_CLANG);
    neo_set_build_dir("build");
    neo_set_profile(NEO_PROFILE_DEBUG);
    neo_set_verbosity(NEO_VERBOSE);
    neo_set_jobs(4);

    // Now all compile/link calls use Clang, output to build/, debug flags
    neo_compile_to_object_file(NEO_GLOBAL_DEFAULT, "hello.c", NULL, NULL, false);
    neo_link(NEO_GLOBAL_DEFAULT, "hello", NULL, false, "hello.o");

    return 0;
}
```

## Scaling Up: The Target Graph

For projects with multiple targets and dependencies, use the graph API:

```c
int main(int argc, char **argv)
{
    neorebuild("neo.c", argv, &argc);

    neo_set_build_dir("build");
    neo_set_profile(NEO_PROFILE_RELEASE);

    neo_graph_t *g = neo_graph_create();

    const char *lib_srcs[] = {"src/util.c", "src/math.c"};
    neo_target_t *lib = neo_add_static_lib(g, "libutil", lib_srcs, 2);
    neo_target_add_cflags(lib, "-Wall");

    const char *app_srcs[] = {"src/main.c"};
    neo_target_t *app = neo_add_executable(g, "myapp", app_srcs, 1);
    neo_target_depends_on(app, lib);

    int rc = neo_graph_build(g);
    neo_graph_destroy(g);
    return rc;
}
```

strata topologically sorts the graph and builds targets in the correct order, running independent compilations in parallel.

## Next Steps

- [Core Concepts](core-concepts.md) -- understand how incremental rebuilds, content hashing, and build profiles work.
- [API Reference](api/global-settings.md) -- full documentation of every function.
