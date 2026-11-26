AC_PREREQ([2.71])

AC_INIT([migash], [0.0.1], [mike@lonelycactus.com], [migash],
        [https://github.com/spk121/migash])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_AUX_DIR([build-aux])

# 'foreign' skips GNU standards files (NEWS, COPYING, etc.)
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects tar-pax])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_AR
AC_CANONICAL_HOST

# Libtool for shared/static libraries
# Enables --enable-shared / --disable-shared
# The 'win32-dll' option says we're using __declspec(dllexport)
LT_INIT([shared disable-static win32-dll])

# Expose the shared/static choice to Makefile.am conditionals
AM_CONDITIONAL([ENABLE_SHARED], [test "x$enable_shared" = xyes])
AM_CONDITIONAL([ENABLE_WIN32_DLL], [test "x$enable_win32_dll" = xyes])

dnl ------------------------------------------------------------------------------
dnl - library configuration options - for migaconf.h and config.h
dnl ------------------------------------------------------------------------------

dnl The misra-c branch builds for strict ISO C only: there is no platform API
dnl to select (see docs/misra-roadmap.md).

dnl Arena memory-usage statistics. MIGA_ARENA_STATS changes the layout of the
dnl public miga_arena_t, so it goes in migaconf.h, not config.h.
AC_ARG_ENABLE([arena-stats],
  [AS_HELP_STRING([--enable-arena-stats],
                  [Track memory-usage statistics in the arena allocator])],
  [arena_stats="$enableval"],
  [arena_stats="no"])
AC_MSG_CHECKING([whether to track arena memory-usage statistics])
AS_IF([test "x$arena_stats" = xyes],
  [ARENA_STATS_DEFINE="#define MIGA_ARENA_STATS 1"],
  [ARENA_STATS_DEFINE="/* #undef MIGA_ARENA_STATS */"])
AC_MSG_RESULT([$arena_stats])
AC_SUBST([ARENA_STATS_DEFINE])

dnl ------------------------------------------------------------------------------
dnl - platform feature checks
dnl ------------------------------------------------------------------------------

CHECK_MAIN_THREE_ARGS

dnl ------------------------------------------------------------------------------
dnl - output files
dnl ------------------------------------------------------------------------------

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 include/Makefile
                 include/miga/migaconf.h
                 migash.pc
                 packaging/migash.spec])
AC_OUTPUT
