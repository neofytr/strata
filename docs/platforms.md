# Platform Support

## Linux

**Status: Full support**

Linux is the primary development platform for strata. All features are fully supported and tested:

- GCC and Clang compilers
- Parallel compilation via `fork()`
- Header dependency tracking via `-MMD`
- Content hash-based change detection
- `pkg-config` package detection
- Shared and static library building with versioned symlinks
- Cross-compilation toolchains
- Test runner with timeouts (via signals)
- Installation to standard FHS paths
- `compile_commands.json` export

Tested on:
- Ubuntu 20.04+
- Debian 11+
- Fedora 36+
- Arch Linux

### Requirements

- GCC 9+ or Clang 10+
- GNU Make (only for building strata itself, not required for user projects)
- `pkg-config` (optional, for package detection)

---

## macOS

**Status: Partial support**

The POSIX layer works on macOS, and most core features function correctly. However, some features have not been extensively tested:

- Basic compilation and linking works with both GCC (via Homebrew) and Apple Clang
- The command API works with all shell types
- Static library building works
- Shared library building produces `.dylib` files (naming conventions may differ)
- `pkg-config` works if installed via Homebrew

### Known Limitations

- Versioned shared library symlinks follow Linux conventions (`.so.X.Y.Z`) and may not match macOS expectations (`.X.dylib`)
- Some signal-based test runner features may behave differently
- The `strata` bootstrapper binary in the repository is built for Linux; macOS users need to compile it from source

### Requirements

- Xcode Command Line Tools or full Xcode install
- Homebrew GCC (if not using Apple Clang)
- `pkg-config` via Homebrew (optional)

---

## Windows

**Status: Not yet supported**

Windows support is planned for a future release. The current codebase relies on POSIX APIs (`fork`, `execvp`, `waitpid`, signals) that are not natively available on Windows.

### Planned Approach

- Use `CreateProcess` instead of `fork`/`execvp`
- Use Windows threads for parallel compilation
- Support MSVC, MinGW-w64, and Clang on Windows
- Adapt install paths for Windows conventions

### Current Workarounds

- **WSL (Windows Subsystem for Linux):** strata works fully under WSL2 since it provides a complete Linux environment.
- **MSYS2/MinGW:** May work with modifications but is not officially tested.
