cmake_minimum_required(VERSION 3.16)

set(CMAKE_COLOR_DIAGNOSTICS OFF)

project(migash VERSION 0.0.1 LANGUAGES C)

# ============================================================================
# Build Configuration Strategy
# ============================================================================
#
# This is the misra-c branch: the shell is built for strict ISO C only, with
# no POSIX or Windows API. See docs/misra-roadmap.md.
#
# Library Strategy: libmigash is always a static library.
# ============================================================================

# The goal is C23, but on MSVC the best we can do it call its "/std:clatest" option.
# set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Build type configuration
# ============================================================================

# Set default build type if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

message(STATUS "Building with ISO C (no platform-specific API)")

# Define build type macros
add_compile_definitions(
    $<$<CONFIG:Debug>:MIGA_DEBUG_BUILD>
    $<$<CONFIG:Release>:MIGA_RELEASE_BUILD>
)

# ============================================================================
# Compiler options
# ============================================================================

option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)
option(ENABLE_ARENA_STATS "Track memory-usage statistics in the arena allocator" OFF)

if(MSVC)
    # The sources use the ISO C functions (fopen, strerror, ...) on purpose;
    # the *_s variants are not portable.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # MSVC: Maximum useful warnings + treat as errors
    add_compile_options(
        /std:clatest    # Latest C standard (C23-ish)
        # /Wall           # Enable almost all warnings (stronger than /W4)
        /W3 		    # Level 3 warnings (practical balance)
        # /WX             # Treat warnings as errors
        /we4028         # Treat formal parameter unreferenced warnings as errors
        /we4029         # Treat redefinition warnings as errors
        /we4133         # Treat incompatible pointer type warnings as errors
        /wd5045         # Disable Spectre mitigation warning
        /wd6011         # Disable warning about dereferencing NULL pointer
        /wd4820         # Disable padding warning
        /wd4061         # Disable enum not handled in switch warning
        /Zi             # Debug info
    )

    # Optimization based on build type
    add_compile_options(
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
        $<$<CONFIG:MinSizeRel>:/Os>
    )

    # Optional: Disable particularly noisy /Wall warnings if needed
    # add_compile_options(/wdXXXX)  # e.g., /wd4201 for nonstandard extension anonymous struct/union

    if(ENABLE_COVERAGE)
        message(FATAL_ERROR "Code coverage is not supported with MSVC")
    elseif(ENABLE_SANITIZERS)
        message(FATAL_ERROR "Sanitizers are not supported with MSVC")
    endif()
else()
    # Common warnings for GCC and Clang
    add_compile_options(
        -std=c2x
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wformat=2
        -Wformat-signedness
        -Wformat-security
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wold-style-definition
        -Wmissing-declarations
        -Wcast-align
        -Wcast-qual
        -Wunused
        -Wvla                   # Warn on variable-length arrays (often a bad idea)
        -Wswitch-default
        -Wswitch-enum
        -Wfloat-equal
        -Wundef
    )

    # Debug info based on build type
    add_compile_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:RelWithDebInfo>:-g>
    )

    if(CI)
        add_compile_options(-Werror)
    endif()

    # Compiler-specific extras
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        add_compile_options(
            -fanalyzer          # GCC static analyzer (very useful, low false positives)
            -Wlogical-op
            -Warith-conversion
            -Wduplicated-cond
            -Wduplicated-branches
        )
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        # Clang already catches many GCC-specific things; add a few extras if desired
        # add_compile_options(-Weverything)  # Only for experimentation – too noisy!
    else()
        message(WARNING "Unknown compiler – using generic non-MSVC flags")
    endif()

    # Coverage, sanitizers, or normal optimization
    if(ENABLE_COVERAGE)
        if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "Coverage only supported with GCC or Clang")
        endif()

        add_compile_options(-O0 --coverage)
        add_link_options(--coverage)
        message(STATUS "Code coverage enabled (-O0 --coverage)")
    elseif(ENABLE_SANITIZERS)
        if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "Sanitizers only supported with GCC or Clang")
        endif()
        add_compile_options(-O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "Sanitizers enabled (-fsanitize=address,undefined)")
    else()
        # Use build-type-appropriate optimization
        add_compile_options(
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Release>:-O2>
            $<$<CONFIG:RelWithDebInfo>:-O2>
            $<$<CONFIG:MinSizeRel>:-Os>
        )
    endif()
endif()


# ============================================================================
# Generated configuration header (include/miga/migaconf.h)
# ============================================================================
#
# Compile-time options that affect the public API. Autotools generates the
# header with configure; here we do the same with configure_file so both build
# systems agree.

# MIGA_ARENA_STATS changes the layout of the public miga_arena_t, so it goes in
# migaconf.h where the library and its users both see it.
if(ENABLE_ARENA_STATS)
    set(ARENA_STATS_DEFINE "#define MIGA_ARENA_STATS 1")
    message(STATUS "Arena memory-usage statistics enabled")
else()
    set(ARENA_STATS_DEFINE "/* #undef MIGA_ARENA_STATS */")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/miga/migaconf.h.in
    ${CMAKE_BINARY_DIR}/include/miga/migaconf.h
    @ONLY
)

set(MIGA_LIBRARY_TYPE STATIC)

# ============================================================================
# libmigash sources
# ============================================================================

set(MIGASH_LIB_SOURCES
    src/alias.c
    src/alias.h
    src/alias_array.c
    src/alias_array.h
    src/alias_store.c
    src/alias_store.h
    src/arithmetic.c
    src/arithmetic.h
    src/ast.c
    src/ast.h
    src/builtin_store.c
    src/builtin_store.h
    src/builtins.c
    src/builtins.h
    src/exec.c
    src/exec_command.c
    src/exec_command.h
    src/exec_frame.c
    src/exec_frame.h
    src/exec_frame_expander.c
    src/exec_frame_expander.h
    src/exec_frame_policy.h
    src/exec_redirect.c
    src/exec_redirect.h
    src/exec_types_internal.h
    src/fd_table.c
    src/fd_table.h
    src/frame.c
    src/func_map.c
    src/func_map.h
    src/func_store.c
    src/func_store.h
    src/getopt.c
    src/glob_util.c
    src/glob_util.h
    src/gnode.c
    src/gnode.h
    src/gprint.c
    src/gprint.h
    src/job_store.c
    src/job_store.h
    src/lexer.c
    src/lexer.h
    src/lexer_arith_exp.c
    src/lexer_arith_exp.h
    src/lexer_cmd_subst.c
    src/lexer_cmd_subst.h
    src/lexer_dquote.c
    src/lexer_dquote.h
    src/lexer_heredoc.c
    src/lexer_heredoc.h
    src/lexer_normal.c
    src/lexer_normal.h
    src/lexer_param_exp.c
    src/lexer_param_exp.h
    src/lexer_priv_t.h
    src/lexer_squote.c
    src/lexer_squote.h
    src/lexer_t.h
    src/lib.c
    src/lib.h
    src/logging.c
    src/logging.h
    src/lower.c
    src/lower.h
    src/parse_session.c
    src/parse_session.h
    src/parser.c
    src/parser.h
    src/pattern_removal.c
    src/pattern_removal.h
    src/positional_params.c
    src/positional_params.h
    src/sig_act.c
    src/sig_act.h
    src/string_t.c
    src/strlist.c
    src/token.c
    src/token.h
    src/token_array.c
    src/token_array.h
    src/tokenizer.c
    src/tokenizer.h
    src/trap_controller.c
    src/trap_controller.h
    src/trap_store.c
    src/trap_store.h
    src/variable_map.c
    src/variable_map.h
    src/variable_store.c
    src/variable_store.h
    src/xalloc.c
)

# The public headers, so that IDEs list them.
set(MIGASH_PUBLIC_HEADERS
    include/libmigash.h
    include/miga/api.h
    include/miga/exec.h
    include/miga/frame.h
    include/miga/getopt.h
    include/miga/mutex.h
    include/miga/string_t.h
    include/miga/strlist.h
    include/miga/type_pub.h
    include/miga/xalloc.h
)

# All library code is compiled once into an object library. The real library
# is assembled from those objects, and the unit tests link the objects
# directly so that they can reach internal (hidden) symbols.
add_library(migash_objs OBJECT ${MIGASH_LIB_SOURCES} ${MIGASH_PUBLIC_HEADERS})
target_include_directories(migash_objs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(migash_lib ${MIGA_LIBRARY_TYPE} $<TARGET_OBJECTS:migash_objs>)
set_target_properties(migash_lib PROPERTIES OUTPUT_NAME migash)
target_include_directories(migash_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
target_link_libraries(migash_lib PUBLIC $<$<BOOL:${UNIX}>:m>)

# ============================================================================
# Executable: migash
# ============================================================================
#
# main.c and shell.c are the shell program. They may use only the public API in
# include/.

add_executable(migash src/main.c src/shell.c src/shell.h)
target_link_libraries(migash PRIVATE migash_lib)
target_include_directories(migash PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)
set_target_properties(migash PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================================
# Testing configuration
# ============================================================================

enable_testing()

# Object library for the shared CTest framework used by all unit tests
add_library(ctest_obj OBJECT
    test/ctest/ctest.c
    test/ctest/ctest.h)
target_include_directories(ctest_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
)

set(MIGASH_UNIT_TEST_SOURCES
    test/mgsh/test_alias_ctest.c
    test/mgsh/test_alist_ctest.c
    test/mgsh/test_arithmetic_ctest.c
    test/mgsh/test_ast_ctest.c
    test/mgsh/test_ast_heredoc_ctest.c
    test/mgsh/test_exec_ctest.c
    test/mgsh/test_fd_table_ctest.c
    test/mgsh/test_func_store_ctest.c
    test/mgsh/test_getopt_ctest.c
    test/mgsh/test_job_store_ctest.c
    test/mgsh/test_lexer_arith_exp_ctest.c
    test/mgsh/test_lexer_cmd_subst_ctest.c
    test/mgsh/test_lexer_heredoc_ctest.c
    test/mgsh/test_lexer_param_exp_ctest.c
    test/mgsh/test_lexer_quotes_ctest.c
    test/mgsh/test_memory_leak_ctest.c
    test/mgsh/test_parser_ctest.c
    test/mgsh/test_parser_gnode_ctest.c
    test/mgsh/test_pattern_removal_ctest.c
    test/mgsh/test_positional_params_ctest.c
    test/mgsh/test_sig_act_ctest.c
    test/mgsh/test_string_ctest.c
    test/mgsh/test_string_list_ctest.c
    test/mgsh/test_token_array_ctest.c
    test/mgsh/test_token_ctest.c
    test/mgsh/test_tokenizer_ctest.c
    test/mgsh/test_variable_store_ctest.c
    test/mgsh/test_xalloc_ctest.c
)

foreach(test_src ${MIGASH_UNIT_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src}
        $<TARGET_OBJECTS:ctest_obj>
        $<TARGET_OBJECTS:migash_objs>)
    # Test-specific C code can be conditionally compiled with #ifdef IN_CTEST
    target_compile_definitions(${test_name} PRIVATE IN_CTEST)
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/test/ctest
    )
    target_link_libraries(${test_name} PRIVATE $<$<BOOL:${UNIX}>:m>)

    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    add_test(NAME ${test_name}
        COMMAND ${test_name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test
    )

    # Without a timeout a hung test stalls CI until the job limit; each unit
    # test finishes in well under a second.
    set_tests_properties(${test_name} PROPERTIES
        FAIL_REGULAR_EXPRESSION "not ok|\\b(Bail out!|Bailout!)\\b"
        PASS_REGULAR_EXPRESSION "ok"
        TIMEOUT 60
    )
endforeach()

# ============================================================================
# Code coverage target
# ============================================================================

if(ENABLE_COVERAGE)
    find_program(GENHTML genhtml)
    find_program(LCOV lcov)

    if(GENHTML AND LCOV)
        add_custom_target(coverage
            COMMAND ${CMAKE_CTEST_COMMAND}  # Run tests
            COMMAND ${LCOV} --capture --directory . --output-file coverage.raw.info
            COMMAND ${LCOV} --remove coverage.raw.info '/usr/*' '*/test/*' '*/src/main.c' '*/src/getopt.c' --output-file coverage.info
            COMMAND ${LCOV} --extract coverage.info '*/src/*.c' --output-file coverage.filtered.info
            COMMAND ${GENHTML} coverage.filtered.info --output-directory coverage_report --title "migash Coverage" --branch-coverage --legend
            COMMENT "Generating coverage report in coverage_report/"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        )
    endif()
endif()

# ============================================================================
# Fun emergency target 😅
# ============================================================================
add_custom_target(wtf
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "🚀  You've got this! Everything is going to be fine. 🌟"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "💪 Keep going — the code will bend to your will eventually. 🔥"
    COMMAND ${CMAKE_COMMAND} -E echo "☕ Take a deep breath, maybe grab a coffee, and come back stronger. ✨"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMENT "Running the WTF target — motivational support activated!"
)

