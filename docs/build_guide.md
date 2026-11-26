# migash Build Guide (misra-c branch)

This branch builds the shell for strict ISO C only: there is no POSIX or
Windows API to choose (see `misra-roadmap.md`). The library, `libmigash`, is
always static.

## Quick Start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

With autotools:

```bash
autoreconf -fi
./configure
make check
```

## Build Configuration Options

### CMAKE_BUILD_TYPE

| Value | Purpose | Optimization |
|-------|---------|--------------|
| `Debug` | Development, debugging | `-O0 -g3` |
| `Release` | Production | `-O2` |
| `RelWithDebInfo` | Profiling | `-O2 -g` |
| `MinSizeRel` | Embedded/minimal | `-Os` |

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
allocator in `xalloc.c` count every allocation, with no external tools. It defines `MIGA_ARENA_STATS` in
the generated `miga/migaconf.h`, because it changes the layout of the public
`miga_arena_t`; code built against the library must use the same header.

```bash
cmake -B build-stats -DENABLE_ARENA_STATS=ON
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

## Testing

```bash
cd build
ctest                      # Brief output
ctest --output-on-failure  # Show failures
./test/test_parser_ctest   # One test program
```

The tests are the unit tests in `test/mgsh/` (TAP output), including the
memory-leak regression test `test_memory_leak_ctest`. The POSIX conformance
scripts in `test/sh/` need fork, pipes and command substitution, so they do
not run against this build; they are kept as the reference for a future ISO C
end-to-end suite.
