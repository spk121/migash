AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

SUBDIRS = src include

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = migash.pc

EXTRA_DIST = migash.pc.in packaging/migash.spec.in \
    test/sh/run_tests.sh test/sh/test_helpers.sh test/sh/README.md \
    $(wildcard $(srcdir)/test/sh/[0-9]*.sh)

# ---------------------------------------------------------------------------
# Tests: "make check"
# ---------------------------------------------------------------------------

# The tiny TAP test framework shared by the unit tests
check_LTLIBRARIES = libctest.la
libctest_la_SOURCES = test/ctest/ctest.c test/ctest/ctest.h

# Unit tests link the convenience library so that internal symbols are visible.
TEST_CPPFLAGS = -DIN_CTEST -I$(top_srcdir)/src -I$(top_srcdir)/include \
    -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/test/ctest
TEST_LDADD = libctest.la src/libmigash_internal.la

check_PROGRAMS = \
    test_alias_ctest \
    test_alist_ctest \
    test_arithmetic_ctest \
    test_ast_ctest \
    test_ast_heredoc_ctest \
    test_exec_ctest \
    test_fd_table_ctest \
    test_func_store_ctest \
    test_getopt_ctest \
    test_job_store_ctest \
    test_lexer_arith_exp_ctest \
    test_lexer_cmd_subst_ctest \
    test_lexer_heredoc_ctest \
    test_lexer_param_exp_ctest \
    test_lexer_quotes_ctest \
    test_parser_ctest \
    test_parser_gnode_ctest \
    test_pattern_removal_ctest \
    test_positional_params_ctest \
    test_sig_act_ctest \
    test_string_ctest \
    test_string_list_ctest \
    test_token_array_ctest \
    test_token_ctest \
    test_tokenizer_ctest \
    test_variable_store_ctest \
    test_xalloc_ctest

test_alias_ctest_SOURCES = test/mgsh/test_alias_ctest.c
test_alias_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_alias_ctest_LDADD = $(TEST_LDADD)

test_alist_ctest_SOURCES = test/mgsh/test_alist_ctest.c
test_alist_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_alist_ctest_LDADD = $(TEST_LDADD)

test_arithmetic_ctest_SOURCES = test/mgsh/test_arithmetic_ctest.c
test_arithmetic_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_arithmetic_ctest_LDADD = $(TEST_LDADD)

test_ast_ctest_SOURCES = test/mgsh/test_ast_ctest.c
test_ast_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_ast_ctest_LDADD = $(TEST_LDADD)

test_ast_heredoc_ctest_SOURCES = test/mgsh/test_ast_heredoc_ctest.c
test_ast_heredoc_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_ast_heredoc_ctest_LDADD = $(TEST_LDADD)

test_exec_ctest_SOURCES = test/mgsh/test_exec_ctest.c
test_exec_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_exec_ctest_LDADD = $(TEST_LDADD)

test_fd_table_ctest_SOURCES = test/mgsh/test_fd_table_ctest.c
test_fd_table_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_fd_table_ctest_LDADD = $(TEST_LDADD)

test_func_store_ctest_SOURCES = test/mgsh/test_func_store_ctest.c
test_func_store_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_func_store_ctest_LDADD = $(TEST_LDADD)

test_getopt_ctest_SOURCES = test/mgsh/test_getopt_ctest.c
test_getopt_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_getopt_ctest_LDADD = $(TEST_LDADD)

test_job_store_ctest_SOURCES = test/mgsh/test_job_store_ctest.c
test_job_store_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_job_store_ctest_LDADD = $(TEST_LDADD)

test_lexer_arith_exp_ctest_SOURCES = test/mgsh/test_lexer_arith_exp_ctest.c
test_lexer_arith_exp_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_lexer_arith_exp_ctest_LDADD = $(TEST_LDADD)

test_lexer_cmd_subst_ctest_SOURCES = test/mgsh/test_lexer_cmd_subst_ctest.c
test_lexer_cmd_subst_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_lexer_cmd_subst_ctest_LDADD = $(TEST_LDADD)

test_lexer_heredoc_ctest_SOURCES = test/mgsh/test_lexer_heredoc_ctest.c
test_lexer_heredoc_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_lexer_heredoc_ctest_LDADD = $(TEST_LDADD)

test_lexer_param_exp_ctest_SOURCES = test/mgsh/test_lexer_param_exp_ctest.c
test_lexer_param_exp_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_lexer_param_exp_ctest_LDADD = $(TEST_LDADD)

test_lexer_quotes_ctest_SOURCES = test/mgsh/test_lexer_quotes_ctest.c
test_lexer_quotes_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_lexer_quotes_ctest_LDADD = $(TEST_LDADD)

test_parser_ctest_SOURCES = test/mgsh/test_parser_ctest.c
test_parser_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_parser_ctest_LDADD = $(TEST_LDADD)

test_parser_gnode_ctest_SOURCES = test/mgsh/test_parser_gnode_ctest.c
test_parser_gnode_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_parser_gnode_ctest_LDADD = $(TEST_LDADD)

test_pattern_removal_ctest_SOURCES = test/mgsh/test_pattern_removal_ctest.c
test_pattern_removal_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_pattern_removal_ctest_LDADD = $(TEST_LDADD)

test_positional_params_ctest_SOURCES = test/mgsh/test_positional_params_ctest.c
test_positional_params_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_positional_params_ctest_LDADD = $(TEST_LDADD)

test_sig_act_ctest_SOURCES = test/mgsh/test_sig_act_ctest.c
test_sig_act_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_sig_act_ctest_LDADD = $(TEST_LDADD)

test_string_ctest_SOURCES = test/mgsh/test_string_ctest.c
test_string_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_string_ctest_LDADD = $(TEST_LDADD)

test_string_list_ctest_SOURCES = test/mgsh/test_string_list_ctest.c
test_string_list_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_string_list_ctest_LDADD = $(TEST_LDADD)

test_token_array_ctest_SOURCES = test/mgsh/test_token_array_ctest.c
test_token_array_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_token_array_ctest_LDADD = $(TEST_LDADD)

test_token_ctest_SOURCES = test/mgsh/test_token_ctest.c
test_token_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_token_ctest_LDADD = $(TEST_LDADD)

test_tokenizer_ctest_SOURCES = test/mgsh/test_tokenizer_ctest.c
test_tokenizer_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_tokenizer_ctest_LDADD = $(TEST_LDADD)

test_variable_store_ctest_SOURCES = test/mgsh/test_variable_store_ctest.c
test_variable_store_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_variable_store_ctest_LDADD = $(TEST_LDADD)

test_xalloc_ctest_SOURCES = test/mgsh/test_xalloc_ctest.c
test_xalloc_ctest_CPPFLAGS = $(TEST_CPPFLAGS)
test_xalloc_ctest_LDADD = $(TEST_LDADD)

# The unit tests print TAP ("ok" / "not ok") and exit non-zero on failure.
# The POSIX conformance scripts run under the freshly built shell.
TESTS = $(check_PROGRAMS) test/sh/run_tests.sh
AM_TESTS_ENVIRONMENT = MIGASH='$(abs_top_builddir)/src/migash'; export MIGASH;
