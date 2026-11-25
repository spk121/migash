# miga Build Guide

## Quick Start

### Development Build (Recommended for Linux)
```bash
# Best configuration for catching bugs during development
cmake -B build-debug -DAPI_TYPE=POSIX -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
cd build-debug && ctest
```

This gives you:
- ✅ Shared libraries with strict layering validation (`--no-undefined`)
- ✅ All warnings enabled, treated as errors in CI
- ✅ Debug symbols (`-g3`)
- ✅ No optimization (`-O0`)
- ✅ Test symbols exported for unit testing

### Release Build
```bash
cmake -B build-release -DAPI_TYPE=POSIX -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

This gives you:
- ✅ Static libraries (lean binary)
- ✅ All internal symbols hidden
- ✅ Optimized (`-O2`)
- ✅ No debug info

### Portability Testing (ISO C)
```bash
cmake -B build-iso -DAPI_TYPE=ISO_C -DCMAKE_BUILD_TYPE=Debug
cmake --build build-iso
```

This gives you:
- ✅ Strict ISO C compliance (no compiler extensions)
- ✅ Static libraries (no layering validation available)
- ❌ No symbol visibility control
- ❌ No strict linking checks

## Build Configuration Options

### API_TYPE
Controls which platform API is used:

| Option | Use Case | Platforms |
|--------|----------|-----------|
| `POSIX` | Development & production on Unix-like systems | Linux, macOS, BSD |
| `UCRT` | Development & production on Windows | Windows 10+ |
| `ISO_C` | Portability verification | All (fallback mode) |

**Default:** `POSIX`

### CMAKE_BUILD_TYPE
Standard CMake build types:

| Type | Optimization | Debug Info | Use Case |
|------|--------------|------------|----------|
| `Debug` | `-O0` | `-g3` | Active development |
| `Release` | `-O2` | None | Production builds |
| `RelWithDebInfo` | `-O2` | `-g` | Profiling, debugging optimized code |
| `MinSizeRel` | `-Os` | None | Size-constrained environments |

**Default:** `Release`

### Special Options

```bash
# Enable code coverage (GCC/Clang only)
cmake -B build-cov -DENABLE_COVERAGE=ON
cmake --build build-cov
cd build-cov && ctest
cmake --build build-cov --target coverage
# Open build-cov/coverage_report/index.html

# Enable sanitizers (GCC/Clang only)
cmake -B build-san -DENABLE_SANITIZERS=ON
cmake --build build-san
cd build-san && ctest  # Will detect memory issues

# CI mode (treat warnings as errors)
cmake -B build-ci -DCI=ON
cmake --build build-ci
```

### Arena Memory-Usage Statistics

`-DENABLE_ARENA_STATS=ON` (autotools: `--enable-arena-stats`) makes the arena
allocator in `xalloc.c` count every allocation. It works in every `API_TYPE`,
ISO C included, and needs no external tools. It defines `MIGA_ARENA_STATS` in
the generated `miga/migaconf.h`, because it changes the layout of the public
`miga_arena_t`; code built against the library must use the same header.

```bash
cmake -B build-stats -DAPI_TYPE=ISO_C -DENABLE_ARENA_STATS=ON
cmake --build build-stats
MIGA_ARENA_STATS=1 build-stats/bin/migash script.sh
```

With `MIGA_ARENA_STATS` set in the environment, the shell prints a report to
stderr at exit:

```
arena: live 0 blocks, 0 bytes; peak 438 blocks, 25719 bytes
arena: 8634 allocs, 8 reallocs, 8634 frees; 353225 bytes allocated in total; largest block 2048 bytes
arena: 0 blocks, 0 bytes still allocated at teardown (1 resets)
arena: allocation sizes: <=16: 722 <=32: 5688 <=64: 1849 <=128: 271 <=256: 3 <=512: 104 <=1024: 3 <=2048: 2
```

- **peak** is the most memory in use at once (sizes as requested, not counting
  the arena's own bookkeeping).
- **still allocated at teardown** counts the blocks that shell cleanup did not
  free before `miga_arena_end()` reclaimed them. These are leaks, so this line
  should read 0. A peak that grows with a script's loop count also points to a
  leak.
- **allocation sizes** is a histogram in power-of-two size classes.

Programs and tests can read the same data through `miga_arena_stats_get()`,
`miga_arena_stats_reset_peak()`, `miga_arena_stats_clear()`,
`miga_arena_stats_foreach_live()` and `miga_arena_stats_print()`, declared in
`miga/xalloc.h`.

## Understanding Library Layering

The project enforces a strict 3-layer architecture:

```
sh23base     (base utilities: strings, memory, logging)
    ↑
sh23store    (data structures: AST, variables, jobs)
    ↑
sh23logic    (shell logic: parser, executor, builtins)
    ↑
sh23interface (convenience interface to all layers)
```

### Layering Validation

When building shared libraries in POSIX mode, the linker enforces layer boundaries:

```bash
# This will FAIL if sh23store tries to use symbols from sh23logic:
cmake -B build -DAPI_TYPE=POSIX -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# Error: undefined reference to `function_from_wrong_layer`
```

This validation is **only active** when:
1. Building shared libraries (Debug mode on Linux)
2. Using POSIX API (not ISO_C)
3. On a platform supporting `--no-undefined` (Linux)

### When Layering Validation Is NOT Active

| Scenario | Reason |
|----------|--------|
| Release builds | Uses static libraries |
| macOS builds | No `--no-undefined` support |
| Windows builds | No `--no-undefined` support |
| ISO_C mode | Can't use visibility attributes |

**Recommendation:** Do primary development on Linux with POSIX + Debug to catch violations early.

## Symbol Visibility

### In Your Code

Include `sh23_visibility.h` and use the macros:

```c
#include "sh23_visibility.h"

// Public API - exposed to users (in debug builds)
SH23_API void shell_execute(const char *cmd);

// Internal helper - never exposed
SH23_INTERNAL void parse_tokens(void);

// Test hook - only exposed when building tests
SH23_TEST_API void test_reset_state(void);
```

### What Gets Exported

| Build Mode      | SH23_API    | SH23_INTERNAL | SH23_TEST_API            |
|-----------------|-------------|---------------|--------------------------|
| Debug (POSIX)   | ✅ Exported | ❌ Hidden    | ✅ Exported (if IN_CTEST) |
| Release (POSIX) | ❌ Hidden   | ❌ Hidden    | ❌ Hidden                |
| ISO_C mode      | ✅ Default  | ✅ Default   | ✅ Default               |

### Verifying Symbol Export

```bash
# Build release
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# Check exported symbols (should be minimal)
nm -D build-release/bin/miga | grep ' T '

# Or with objdump
objdump -T build-release/bin/miga | grep GLOBAL
```

## Testing

### Run All Tests
```bash
cd build-debug
ctest                    # Brief output
ctest --output-on-failure  # Show failures
ctest -V                 # Verbose
```

### Run Specific Test
```bash
cd build-debug/test
./test_parser_ctest
```

### Test Organization

Tests are organized by layer:
- `SH23BASE_TEST_SOURCES` - Only link against `sh23base`
- `SH23STORE_TEST_SOURCES` - Link against `sh23store` + `sh23base`
- `SH23LOGIC_TEST_SOURCES` - Link against full `sh23interface`

This ensures tests respect the layering architecture.

## Common Workflows

### Daily Development (Linux)
```bash
cmake -B build -DAPI_TYPE=POSIX -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

### Before Committing
```bash
# Test portability
cmake -B build-iso -DAPI_TYPE=ISO_C
cmake --build build-iso
cd build-iso && ctest

# Test release build
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel
cd build-rel && ctest
```

### Deep Bug Hunting
```bash
# With sanitizers
cmake -B build-san -DENABLE_SANITIZERS=ON
cmake --build build-san
cd build-san && ctest

# With coverage
cmake -B build-cov -DENABLE_COVERAGE=ON
cmake --build build-cov
cd build-cov && ctest
cmake --build build-cov --target coverage
```

### Emergency Motivation 😅
```bash
cmake --build build --target wtf
```

## Troubleshooting

### "undefined reference" errors in Debug build
This is **intentional** - you've violated the layering architecture. The linker is catching a dependency that shouldn't exist. Fix the code to respect the layer boundaries.

### Warnings treated as errors
In CI mode (`-DCI=ON`), all warnings become errors. Fix the warnings or disable CI mode for local development.

### ISO_C mode build issues
ISO_C mode is intentionally restrictive. Some POSIX or UCRT features won't be available. This is for portability testing only - use POSIX/UCRT for actual development.

### Coverage report not generating
Requires `lcov` and `genhtml` installed:
```bash
# Ubuntu/Debian
sudo apt-get install lcov

# macOS
brew install lcov
```

### Sanitizer false positives
Sanitizers can be noisy. You can suppress specific warnings by setting `ASAN_OPTIONS` or `UBSAN_OPTIONS` environment variables. See the AddressSanitizer/UBSan documentation for details.

## Platform-Specific Notes

### Linux
- Best platform for development
- Full layering validation available
- All tools supported (coverage, sanitizers, static analysis)

### macOS
- Use POSIX mode
- No `--no-undefined` support (layering validation unavailable)
- Static libraries only

### Windows
- Use UCRT mode
- Use Visual Studio or MinGW
- Static libraries recommended
- Some warnings differ from GCC/Clang

### Other Unix (BSD, etc.)
- Use POSIX mode
- May need to adjust compiler flags
- Check if `--no-undefined` is supported
