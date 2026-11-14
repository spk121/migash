#ifndef BUILTINS_H
#define BUILTINS_H

#include "miga/type_pub.h"
#include "miga/frame.h"
#include "miga/strlist.h"
#include "miga/string_t.h"

/* ============================================================================
 * Builtin Commands
 * ============================================================================
 *
 * Builtin commands are implemented at the shell abstraction level and have
 * access to the full shell context. They return an exit status code.
 *
 * Return values:
 *   0 - success
 *   1 - general error
 *   2 - misuse of shell builtin
 *   other - command-specific error codes
 */

int builtin_colon(miga_frame_t *frame, const strlist_t *args);

int builtin_dot(miga_frame_t *frame, const strlist_t *args);

int builtin_eval(miga_frame_t *frame, const strlist_t *args);

int builtin_export(miga_frame_t *frame, const strlist_t *args);

int builtin_readonly(miga_frame_t *frame, const strlist_t *args);

int builtin_times(miga_frame_t *frame, const strlist_t *args);

int builtin_trap(miga_frame_t *frame, const strlist_t *args);

int builtin_return(miga_frame_t *frame, const strlist_t *args);

int builtin_break(miga_frame_t *frame, const strlist_t *args);

int builtin_continue(miga_frame_t *frame, const strlist_t *args);

int builtin_exec(miga_frame_t *frame, const strlist_t *args);

int builtin_exit(miga_frame_t *frame, const strlist_t *args);

int builtin_shift(miga_frame_t *frame, const strlist_t *args);

/**
 * set - Set or unset shell options and positional parameters
 *
 * @param frame The execution frame context
 * @param args The argument list (including "set" as args[0])
 * @return Exit status code
 */
int builtin_set(miga_frame_t *frame, const strlist_t *args);

int builtin_unset(miga_frame_t *frame, const strlist_t *args);

/* read [-r] var... : one line from standard input into variables */
int builtin_read(miga_frame_t *frame, const strlist_t *args);

/* command [-p] [-v | -V] name... : -v/-V describe a name (the executor runs
 * "command name args" itself); type name... : describe names */
int builtin_command(miga_frame_t *frame, const strlist_t *args);
int builtin_type(miga_frame_t *frame, const strlist_t *args);

/* hash [-r] [utility...] : no table to keep; operands are still looked up */
int builtin_hash(miga_frame_t *frame, const strlist_t *args);
#ifdef MIGA_POSIX_API
/* ulimit [-f] [blocks] : the file size limit */
int builtin_ulimit(miga_frame_t *frame, const strlist_t *args);
#endif

#ifdef MIGA_POSIX_API
/* umask [-S] [mask] : get or set the file mode creation mask */
int builtin_umask(miga_frame_t *frame, const strlist_t *args);
#endif

/* ============================================================================
 * Regular (non-special) built-ins
 * ============================================================================
 */
int builtin_cd(miga_frame_t *frame, const strlist_t *args);
int builtin_pwd(miga_frame_t *frame, const strlist_t *args);

int builtin_echo(miga_frame_t *frame, const strlist_t *args);
int builtin_printf(miga_frame_t *frame, const strlist_t *args);

int builtin_bracket(miga_frame_t *frame, const strlist_t *args);
int builtin_test(miga_frame_t *frame, const strlist_t *args);

int builtin_jobs(miga_frame_t *frame, const strlist_t *args);
int builtin_kill(miga_frame_t *frame, const strlist_t *args);
int builtin_wait(miga_frame_t *frame, const strlist_t *args);
int builtin_fg(miga_frame_t *frame, const strlist_t *args);
int builtin_bg(miga_frame_t *frame, const strlist_t *args);

int builtin_getopts(miga_frame_t *frame, const strlist_t *args);
int builtin_ls(miga_frame_t *frame, const strlist_t *args);

int builtin_alias(miga_frame_t *frame, const strlist_t *args);
int builtin_unalias(miga_frame_t *frame, const strlist_t *args);

int builtin_basename(miga_frame_t *frame, const strlist_t *args);
int builtin_dirname(miga_frame_t *frame, const strlist_t *args);
int builtin_miga_dirnamevar(miga_frame_t *frame, const strlist_t *args);
int builtin_miga_printfvar(miga_frame_t *frame, const strlist_t *args);
int builtin_miga_cat(miga_frame_t *frame, const strlist_t *args);

int builtin_true(miga_frame_t *frame, const strlist_t *args);
int builtin_false(miga_frame_t *frame, const strlist_t *args);

#endif /* BUILTINS_H */
