# Core Concepts

## Build Graph

At the heart of neobuild is a directed acyclic graph (DAG) of build targets. You create a graph with `neo_graph_create()`, add targets (executables, libraries, custom commands), declare dependencies between them, and call `neo_graph_build()`.

neobuild topologically sorts the graph and builds targets in dependency order. Independent targets are built in parallel, respecting the job limit set by `neo_set_jobs()`.

```
libparser.a ──┐
              ├──> myapp
libutil.a ────┘
```

You can visualize the graph by exporting to Graphviz DOT format:

```c
neo_graph_export_dot(g, "build_graph.dot");
// Then: dot -Tpng build_graph.dot -o build_graph.png
```

## Targets and Dependencies

Each target has:

- **A name** -- used to identify the target and derive output filenames.
- **A type** -- `NEO_TARGET_EXECUTABLE`, `NEO_TARGET_STATIC_LIB`, `NEO_TARGET_SHARED_LIB`, `NEO_TARGET_OBJECT`, or `NEO_TARGET_CUSTOM`.
- **Source files** -- the C files to compile (not applicable to custom targets).
- **Compiler and linker flags** -- per-target overrides.
- **Include directories** -- added as `-I` flags.
- **Dependencies** -- other targets that must be built first.

When target A depends on target B (via `neo_target_depends_on(A, B)`), neobuild ensures B is built before A. If B is a library, it is automatically linked into A.

## Incremental Rebuilds

neobuild avoids unnecessary work by tracking what has changed since the last build.

### Timestamp-Based (Default)

By default, neobuild compares modification timestamps of source files, header dependencies, and output files. If a source file or any header it includes is newer than the corresponding object file, that source is recompiled.

Header dependencies are tracked using the compiler's `-MMD` flag, which generates `.d` dependency files alongside object files.

### Content Hash-Based

Enable content hashing for more accurate change detection:

```c
neo_graph_enable_content_hash(g);
```

With content hashing, neobuild computes a hash of each source file's contents. A file is recompiled only if its content has actually changed, not merely because its timestamp was updated (e.g., by `touch` or a version control checkout).

The hash database is stored alongside the build artifacts.

## Build Profiles

Build profiles inject standard compiler flags automatically:

| Profile | Flags |
|---------|-------|
| `NEO_PROFILE_NONE` | No automatic flags |
| `NEO_PROFILE_DEBUG` | `-g -O0` (debug symbols, no optimization) |
| `NEO_PROFILE_RELEASE` | `-O2 -DNDEBUG` (optimized, assertions disabled) |
| `NEO_PROFILE_RELDBG` | `-O2 -g` (optimized with debug symbols) |

Set the profile globally:

```c
neo_set_profile(NEO_PROFILE_RELEASE);
```

Profile flags are prepended to any per-target flags, so you can still add additional flags on top.

## Parallel Compilation

neobuild compiles independent source files in parallel using `fork()`. Control the degree of parallelism with:

```c
neo_set_jobs(8);  // up to 8 parallel compilations
```

Within the graph API, parallelism applies both to compiling sources within a single target and to building independent targets concurrently.

The low-level `neo_compile_parallel()` function also supports parallel compilation outside the graph API.

## The Strata Bootstrap

`strata` is the bootstrapper binary included in the neobuild repository. Its job is simple:

1. Compile your build file (`neo.c`) against the neobuild library.
2. Execute the resulting binary.

On subsequent runs, `strata` checks whether `neo.c` has been modified and recompiles it if needed.

Your build file itself can also detect its own modification via `neorebuild()`:

```c
neorebuild("neo.c", argv, &argc);
```

This call checks if the build file has changed since it was last compiled. If so, it recompiles and re-executes itself, passing through the original arguments. This means even if you run the compiled build binary directly (rather than via `strata`), it will still pick up changes.

## Build Directory

By default, object files and build artifacts are placed alongside source files. Set a dedicated build directory to keep your source tree clean:

```c
neo_set_build_dir("build");
```

All generated `.o` files, `.d` dependency files, libraries, and executables will be placed under the specified directory.

## Compiler Selection

neobuild supports several compilers:

| Enum Value | Compiler |
|------------|----------|
| `NEO_GCC` | GCC (gcc) |
| `NEO_CLANG` | Clang (clang) |
| `NEO_GPP` | G++ (g++) |
| `NEO_CLANGPP` | Clang++ (clang++) |
| `NEO_LD` | Linker (ld) |
| `NEO_AS` | Assembler (as) |
| `NEO_GLOBAL_DEFAULT` | Use the global default compiler |

Set the global default and use it everywhere:

```c
neo_set_global_default_compiler(NEO_CLANG);
neo_compile_to_object_file(NEO_GLOBAL_DEFAULT, "main.c", NULL, NULL, false);
```

Or set per-target compilers in the graph API:

```c
neo_target_set_compiler(target, NEO_GCC);
```
