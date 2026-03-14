AC_PREREQ([2.71])

AC_INIT([migash], [0.0.1], [mike@lonelycactus.com], [migash],
        [https://github.com/spk121/migash])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

# 'foreign' skips GNU standards files (NEWS, COPYING, etc.)
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects tar-pax])

# Checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AM_PROG_AR

# Libtool for shared/static libraries
# Enables --enable-shared / --disable-shared
# The 'win32-dll' option says we're using __declspec(dllexport)
LT_INIT([shared disable-static win32-dll])

# Expose the shared/static choice to Makefile.am conditionals
AM_CONDITIONAL([ENABLE_SHARED], [test "x$enable_shared" = xyes])
AM_CONDITIONAL([ENABLE_WIN32_DLL], [test "x$enable_win32_dll" = xyes])

AC_ARG_ENABLE([configuration],
  [AS_HELP_STRING([--enable-configuration={posix,ucrt,iso_c}],
                  [Select API configuration (default: iso_c)])],
  [configuration="$enableval"],
  [configuration="iso_c"]
)

case "$configuration" in
  posix)
    CONFIG_DEFINE="#define MGSH_POSIX_API"
    ;;
  ucrt)
    CONFIG_DEFINE="#define MGSH_UCRT_API"
    ;;
  iso_c)
    CONFIG_DEFINE="#define MGSH_ISO_C_API"
    ;;
  *)
    AC_MSG_ERROR([Invalid value for --enable-configuration: $configuration])
    ;;
esac

AC_SUBST([CONFIG_DEFINE])

# Platform feature checks (add as needed)
AC_CHECK_HEADERS([stdlib.h stdio.h])

# Output files
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 include/Makefile
                 include/migash/config.h
                 migash.pc
                 packaging/migash.spec])
AC_OUTPUT
