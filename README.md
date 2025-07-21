# strata: A Build System for C/C++

**strata** is a lightweight build system for C/C++ projects, designed to simplify compilation processes and provide a clean interface for managing builds.

Currently supports only POSIX-compliant systems including Linux

## Installation

Clone the repository:

```bash
git clone https://github.com/neofytr/neobuild.git
cd neobuild
```

Include the header in your build file:

```c
#include "release/neobuild.h"
```

## Quick Start

Here's a simple example of how to use NeoBuild:

```c
#include "release/neobuild.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    // Rebuild the build system itself if neo.c has changed
    neorebuild("neo.c", argv);
    
    // Compile main.c to an object file using GCC
    neo_compile_to_object_file(GCC, "main.c", NULL, NULL, true);

    // Link with glibc to create main using GCC
    neo_link(GCC, "main", NULL, true, "main.o");
    
    return EXIT_SUCCESS;
}
```

## Core Components

### Compilers

```c
typedef enum
{
    GCC,             // GNU Compiler Collection
    CLANG,           // Clang compiler
    GLOBAL_DEFAULT,  // Use globally set default compiler
} neocompiler_t;

// Set/get the global default compiler
void neo_set_global_default_compiler(neocompiler_t compiler);
neocompiler_t neo_get_global_default_compiler();
```

### Command Execution

```c
// Shell types
typedef enum
{
    DASH, // Dash shell
    BASH, // Bash shell
    SH    // Standard shell (sh)
} neoshell_t;

// Create a command
neocmd_t *neocmd_create(neoshell_t shell);

// Add arguments to a command
#define neocmd_append(neocmd_ptr, ...) neocmd_append_null((neocmd_ptr), __VA_ARGS__, NULL)
bool neocmd_append_null(neocmd_t *neocmd, ...);

// Execute commands
pid_t neocmd_run_async(neocmd_t *neocmd);
bool neocmd_run_sync(neocmd_t *neocmd, int *status, int *code, bool print_status_desc);

// Clean up
bool neocmd_delete(neocmd_t *neocmd);
```

### Build Management

```c
// Detect build file changes and rebuild if needed
bool neorebuild(const char *build_file, char **argv);

// Compile source files to object files
bool neo_compile_to_object_file(neocompiler_t compiler, const char *source, 
                               const char *output, const char *compiler_flags, 
                               bool force_compilation);

// Link source files
bool neo_link(neocompiler_t compiler, const char *executable, const char *linker_flags, bool forced_linking, ...);
```

### Configuration

```c
// Parse configuration from files
neoconfig_t *neo_parse_config(const char *config_file_path, size_t *config_arr_len);

// Parse configuration from command line arguments
neoconfig_t *neo_parse_config_arg(char **argv, size_t *config_arr_len);

// Free configuration resources
bool neo_free_config(neoconfig_t *config_arr, size_t config_arr_len);
```

### Logging

```c
typedef enum
{
    ERROR,   // Critical issues
    WARNING, // Potential issues 
    INFO,    // General information
    DEBUG    // Detailed debugging information
} neolog_level_t;

// Log a message with specific severity
#define NEO_LOG(level, msg) // See header for implementation
```

## Advanced Usage Examples

### Creating and Running Commands

```c
// Create a command for bash
neocmd_t *cmd = neocmd_create(BASH);

// Add arguments
neocmd_append(cmd, "gcc", "-c", "main.c", "-o", "main.o");

// Run synchronously
int status, code;
neocmd_run_sync(cmd, &status, &code, true);

// Clean up
neocmd_delete(cmd);
```

### Asynchronous Execution

```c
neocmd_t *cmd = neocmd_create(SH);
neocmd_append(cmd, "make", "all");

// Run in background
pid_t pid = neocmd_run_async(cmd);

// Do other work...

// Wait for completion later
int status, code;
neoshell_wait(pid, &status, &code, true);

neocmd_delete(cmd);
```

### Configuration Management

```c
// Parse configuration from a file
size_t config_len;
neoconfig_t *config = neo_parse_config("build.conf", &config_len);

// Use configuration...

// Clean up
neo_free_config(config, config_len);
```

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.