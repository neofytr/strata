# strata

> Write your build logic in C. No DSL. No YAML. No Makefiles.

strata is a build system where your build file is a plain C program. You `#include` a single header, call the API to describe compilations, link steps, and dependency graphs, and the build system handles the rest.

## Key Features

- **Pure C build files** -- if you know C, you know strata. No new language to learn.
- **Target graph** with automatic topological sorting and parallel builds.
- **Incremental rebuilds** via timestamp tracking or content hashing.
- **Header dependency tracking** -- changes to included headers trigger recompilation.
- **Build profiles** -- debug, release, and release-with-debug-info presets.
- **Package detection** -- find system libraries via `pkg-config`.
- **Cross-compilation** -- first-class toolchain support.
- **Test runner** -- run test suites with timeouts and result reporting.
- **Install targets** -- install binaries, libraries, and headers to standard paths.
- **IDE integration** -- export `compile_commands.json` for clangd/LSP.
- **Build graph visualization** -- export to Graphviz DOT format.
- **ccache support** -- transparent compiler caching.
- **Self-rebuilding** -- the build file recompiles itself when modified.

## Quick Install

```bash
git clone https://github.com/neofytr/strata.git
cd strata
```

The repository includes `strata`, a pre-built bootstrapper. Write a C build file, run `./strata`, and you are building.

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

```bash
./strata    # compiles neo.c, then executes it
./hello     # run your program
```

## Next Steps

- [Getting Started](getting-started.md) -- installation, prerequisites, and your first build file.
- [Core Concepts](core-concepts.md) -- how the build graph, incremental rebuilds, and profiles work.
- [API Reference](api/global-settings.md) -- complete function-by-function documentation.
- [Examples](examples.md) -- real-world build file patterns.
