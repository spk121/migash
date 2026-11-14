# Lowering rules: grammar tree (gnode) to execution AST

`lower.c` turns the tree the parser builds (`gnode_t`, one node per
production of the POSIX shell grammar, XCU 2.10) into the execution AST
(`ast_node_t`, `ast.h`) that `exec_frame_execute_dispatch()` runs. The
entry point is

```
ast_t *ast_lower(const gnode_t *root);   /* root must be G_PROGRAM */
```

The gnode tree is the parser's property; the lowering never keeps a pointer
into it. Every token that ends up in the AST is cloned (`token_clone()`),
and the caller destroys the gnode tree afterwards.

Conventions in this file: a rule's heading names the grammar node and the
AST it produces. "list" means the node's `data.list`, "a/b/c/d" its
`data.multi` fields, "child" its `data.child`. Every function returns NULL
on a malformed tree after logging an error; the caller treats NULL as
"nothing to execute" (`exec_frame_string_core()` returns EMPTY).

## Execution AST in brief

| Node | Meaning |
|------|---------|
| `AST_COMMAND_LIST` | items plus one separator per item: `CMD_EXEC_SEQUENTIAL` (`;`, newline), `CMD_EXEC_BACKGROUND` (`&`), `CMD_EXEC_END` (last item) |
| `AST_AND_OR_LIST` | `left op right`, `op` is `ANDOR_OP_AND` or `ANDOR_OP_OR` |
| `AST_PIPELINE` | two or more commands, or one command with `is_negated` (`!`) |
| `AST_SIMPLE_COMMAND` | words, assignments, redirections (all as tokens / `AST_REDIRECTION`) |
| `AST_SUBSHELL`, `AST_BRACE_GROUP` | body is an `AST_COMMAND_LIST` |
| `AST_IF_CLAUSE` | `condition`, `then_body`, optional `else_body` |
| `AST_WHILE_CLAUSE`, `AST_UNTIL_CLAUSE` | `condition`, `body` |
| `AST_FOR_CLAUSE` | variable name, optional word list (tokens), `body` |
| `AST_CASE_CLAUSE` | subject word (token), `case_items` |
| `AST_CASE_ITEM` | pattern tokens, optional body, `action` |
| `AST_FUNCTION_DEF` | name, body (a compound command), optional redirections |
| `AST_REDIRECTED_COMMAND` | a compound command plus the redirections that follow it |
| `AST_REDIRECTION` | one `[n]op target` |

Single commands are never wrapped for the sake of it: a list with one
item collapses to the item, a pipeline with one command collapses to the
command, an and-or with no operator collapses to the pipeline. Code that
executes a node must therefore accept any node kind where the grammar
allows a command (`exec_frame_execute_dispatch()` does).

## program, complete_commands, complete_command

```
program           : linebreak complete_commands linebreak | linebreak
complete_commands : complete_command (NEWLINE+ complete_command)*
complete_command  : list [separator_op]
```

- `G_PROGRAM` with no child (blank input) lowers to NULL.
- `G_COMPLETE_COMMANDS` always yields one `AST_COMMAND_LIST`. With one
  complete command the result of lowering it is returned as is when it is
  already a command list, otherwise it is wrapped in a list with separator
  `CMD_EXEC_END`. With several, their lists are flattened into one list
  (ownership of the items moves; the inner lists are destroyed).
- `G_COMPLETE_COMMAND` (a = list, b = optional `G_SEPARATOR_OP`) lowers the
  list, then applies the trailing separator to the last item: `&` makes it
  `CMD_EXEC_BACKGROUND`, `;` makes it `CMD_EXEC_SEQUENTIAL`, none leaves
  `CMD_EXEC_END`. A one-item list whose only separator is `CMD_EXEC_END` is
  unwrapped, so `echo hello` lowers to a bare `AST_SIMPLE_COMMAND`.

## list and term

```
list : and_or (separator_op and_or)*          -- at top level
term : and_or (separator and_or)*             -- inside compound commands
```

Both lower to `AST_COMMAND_LIST`. The list layout is
`[and_or, sep, and_or, sep, ...]`; an element is either `G_AND_OR` or,
when there is no `&&`/`||`, a bare `G_PIPELINE` (the parser does not wrap
single pipelines). The separator after each item becomes that item's
separator: `G_SEPARATOR_OP` is `&` (background) or `;` (sequential); in a
term the `G_SEPARATOR` wrapper holds either a `G_SEPARATOR_OP` or a
`G_NEWLINE_LIST` (sequential). An item with nothing after it gets
`CMD_EXEC_END`.

(Until 2026-11-14 the term lowering mapped every separator to sequential,
so `{ cmd & other; }` and `if ...; then cmd & fi` ran `cmd` in the
foreground.)

## compound_list

```
compound_list : linebreak term [separator]
```

`G_COMPOUND_LIST` (pair.left = `G_TERM`, pair.right = optional
`G_SEPARATOR`) lowers the term as above and then applies the trailing
separator to the last item exactly like `complete_command` does: `&` makes
the last command a background job, `;` or a newline list makes it
sequential, nothing leaves `CMD_EXEC_END`. Unlike `complete_command`, a
compound list is never unwrapped: the body of a compound command is always
an `AST_COMMAND_LIST`.

## and_or

```
and_or : pipeline | and_or AND_IF linebreak pipeline | and_or OR_IF linebreak pipeline
```

The parser builds a left-leaning binary tree: `G_AND_OR` with a = left
(`G_AND_OR` or `G_PIPELINE`), b = a `G_AND_OR` node holding the operator
token, c = right (`G_PIPELINE`, or `G_AND_OR`). Lowering mirrors it as a
binary `AST_AND_OR_LIST` tree; `a && b || c` becomes `((a && b) || c)`. A
degenerate `G_AND_OR` with only a pipeline in `a` lowers to the pipeline.

## pipeline and pipe_sequence

```
pipeline      : [Bang] pipe_sequence
pipe_sequence : command ('|' linebreak command)*
```

`G_PIPELINE`'s list is `[pipe_sequence]` or `[bang, pipe_sequence]`, where
the bang is a `G_WORD_NODE` holding `TOKEN_BANG`. The pipe sequence's list
holds `G_COMMAND` nodes with `G_WORD_NODE` pipe tokens between them, which
are skipped. One command without `!` lowers to the command itself; two or
more commands, or one command with `!`, lower to `AST_PIPELINE(commands,
is_negated)`. The executor applies `!` once to the pipeline's status.

## command

```
command : simple_command
        | compound_command
        | compound_command redirect_list
        | function_definition
```

`G_COMMAND` is a wrapper. In its plain form the command is `data.child`.
For `compound_command redirect_list` the parser nests a second `G_COMMAND`
wrapper with payload `MULTI`: a = the compound command, b =
`G_REDIRECT_LIST`. That form lowers to
`AST_REDIRECTED_COMMAND(command, redirections)`, with each `G_IO_REDIRECT`
of the list lowered by the rule below; an empty redirect list yields the
bare command. The executor runs a redirected command in a brace-group
frame that applies the redirections around the command. Dispatch on the
child's kind: `G_SIMPLE_COMMAND`, `G_SUBSHELL`, `G_BRACE_GROUP`, the five
clause kinds, `G_FUNCTION_DEFINITION`, `G_COMPOUND_COMMAND` (a wrapper
that is unwrapped) or a nested `G_COMMAND`.

## simple_command

```
simple_command : cmd_prefix cmd_word cmd_suffix
               | cmd_prefix cmd_word | cmd_prefix
               | cmd_name cmd_suffix | cmd_name
```

`G_SIMPLE_COMMAND`'s list holds, in order: `G_CMD_PREFIX` wrappers (each
around one `G_ASSIGNMENT_WORD` or `G_IO_REDIRECT`), the `G_CMD_NAME` /
`G_CMD_WORD` / `G_WORD_NODE` for the command name, and a `G_CMD_SUFFIX`
whose list holds further words (`G_CMD_WORD`, `G_WORD_NODE`) and
`G_IO_REDIRECT`s. Lowering sorts them into three sequences and builds
`AST_SIMPLE_COMMAND(words, redirections, assignments)`:

- assignment words keep their `TOKEN_ASSIGNMENT_WORD` tokens (the
  tokenizer has already decided which words are assignments: only those
  before the command name);
- words are tokens in command-line order, the command name first;
- redirections are `AST_REDIRECTION` nodes in command-line order, whether
  they came from the prefix or the suffix. Their order matters (`>f 2>&1`).

A command with no words is an assignment-only command, which the executor
treats specially (XCU 2.9.1).

## compound_command, subshell, brace_group

`G_COMPOUND_COMMAND` is a wrapper around one of `G_SUBSHELL`,
`G_BRACE_GROUP`, `G_FOR_CLAUSE`, `G_CASE_CLAUSE`, `G_IF_CLAUSE`,
`G_WHILE_CLAUSE`, `G_UNTIL_CLAUSE` and is unwrapped.

- `G_SUBSHELL` (a = `(`, b = compound_list, c = `)`) lowers to
  `AST_SUBSHELL(compound_list)`.
- `G_BRACE_GROUP` (a = `{`, b = compound_list or NULL, c = `}`) lowers to
  `AST_BRACE_GROUP(compound_list)`; `{ }` gets an empty command list.

## if_clause and else_part

```
if_clause : If compound_list Then compound_list else_part Fi
          | If compound_list Then compound_list Fi
else_part : Elif compound_list Then compound_list
          | Elif compound_list Then compound_list else_part
          | Else compound_list
```

The parser nests `G_IF_CLAUSE`: the outer node has a = an inner
`G_IF_CLAUSE` (b = condition, d = then body), b = optional `G_ELSE_PART`.
Lowering produces `AST_IF_CLAUSE(condition, then_body)` and fills
`else_body` from the else part:

- `G_ELSE_PART` with only a: a plain `else`; lowers to the compound list.
- `G_ELSE_PART` with a = condition, b = then body, c = optional next else
  part: an `elif`; lowers to a nested `AST_IF_CLAUSE` whose `else_body` is
  the lowered rest of the chain.

So `if A; then x; elif B; then y; else z; fi` becomes
`IF(A, x, else: IF(B, y, else: z))`. The `elif_list` field of
`AST_IF_CLAUSE` exists but is not used by the lowering; the executor
handles both representations.

## while_clause, until_clause, do_group

```
while_clause : While compound_list do_group
until_clause : Until compound_list do_group
do_group     : Do compound_list Done
```

`G_WHILE_CLAUSE` / `G_UNTIL_CLAUSE` (a = keyword, b = condition,
c = `G_DO_GROUP`) lower to `AST_WHILE_CLAUSE(condition, body)` /
`AST_UNTIL_CLAUSE(condition, body)`. `G_DO_GROUP` (a = `do`,
b = compound_list, c = `done`) lowers to its compound list.

## for_clause

```
for_clause : For name linebreak do_group
           | For name linebreak in sequential_sep do_group
           | For name linebreak in wordlist sequential_sep do_group
```

`G_FOR_CLAUSE` (a = `for`, b = `G_NAME_NODE`, c = optional `G_IN_NODE`,
d = `G_DO_GROUP`). The variable name is the name token's text. The word
list is NULL when there is no `in` clause, or when `G_IN_NODE` carries only
the `in` token; when `G_IN_NODE` has payload `MULTI` its b is a
`G_WORDLIST` whose `G_WORD_NODE` tokens are cloned into the list. Result:
`AST_FOR_CLAUSE(name, words_or_NULL, body)`. The executor substitutes
`"$@"` for a NULL word list and expands the words at run time.

## case_clause, case_item, case_item_ns

```
case_clause  : Case WORD linebreak in linebreak case_list Esac
             | Case WORD linebreak in linebreak case_list_ns Esac
             | Case WORD linebreak in linebreak Esac
case_item    : ['('] pattern ('|' pattern)* ')' [compound_list] (DSEMI | SEMI_AND) linebreak
case_item_ns : ['('] pattern ('|' pattern)* ')' [compound_list]
```

`G_CASE_CLAUSE` is nested like the if clause: the outer node has a = an
inner `G_CASE_CLAUSE` (a = `case`, b = the subject `G_WORD_NODE`, c = `in`,
d = `esac`), b = `G_CASE_LIST` or `G_CASE_LIST_NS` or NULL. (Two older
layouts, where the word sits directly in a or b, are still accepted.) The
subject token is cloned into `AST_CASE_CLAUSE(subject)` and each item of
the list is appended to `case_items`:

- `G_CASE_ITEM`: outer node a = inner item (a = optional `(`,
  b = `G_PATTERN_LIST`, c = `)`, d = optional compound_list), b = the
  terminator token. The pattern list's `G_WORD_NODE`s are cloned, skipping
  the `|` tokens the parser left in the list. `action` is
  `CASE_ACTION_BREAK` for `;;`, `CASE_ACTION_FALLTHROUGH` for `;&` (the
  terminator token is `TOKEN_SEMIAND`).
- `G_CASE_ITEM_NS` (the last item, with no terminator) has the inner
  layout directly and gets `CASE_ACTION_NONE`.

The subject word and the patterns stay as tokens: the executor expands
both (without field splitting or pathname expansion) and escapes quoted
pattern characters so that they match literally.

## function_definition

```
function_definition : fname '(' ')' linebreak function_body
function_body       : compound_command [redirect_list]
```

`G_FUNCTION_DEFINITION` (a = `G_FNAME`, d = `G_FUNCTION_BODY` or, in an
older layout, the compound command itself). `G_FUNCTION_BODY` has a = the
compound command and b = optional `G_REDIRECT_LIST`. Result:
`AST_FUNCTION_DEF(name, body, redirections_or_NULL)`. The executor stores
the body and its redirections in the function table; the redirections are
applied on every call.

## redirect_list, io_redirect, io_file, io_here

```
io_redirect : [IO_NUMBER | IO_LOCATION] (io_file | io_here)
io_file     : ('<' | '<&' | '>' | '>&' | '>>' | '<>' | '>|') filename
io_here     : ('<<' | '<<-') here_end
```

`G_IO_REDIRECT` (a = optional `G_IO_NUMBER_NODE`, b = optional
`G_IO_LOCATION_NODE`, c = `G_IO_FILE` or `G_IO_HERE`) lowers to one
`AST_REDIRECTION`:

- `io_number` is the IO_NUMBER token's value, or -1 for the operator's
  default (0 for input, 1 for output; the executor decides). An
  `{name}` IO_LOCATION is kept as `fd_string`; if it is all digits it also
  supplies `io_number`.
- For `G_IO_FILE` (a = operator `G_WORD_NODE`, b = `G_FILENAME`) the
  operator maps to `REDIR_READ`, `REDIR_WRITE`, `REDIR_APPEND`,
  `REDIR_FD_DUP_IN`, `REDIR_FD_DUP_OUT`, `REDIR_READWRITE`,
  `REDIR_WRITE_FORCE`. The target kind is decided from the operator and
  the target word's text: for `<&` and `>&`, `-` is `REDIR_TARGET_CLOSE`,
  all digits is `REDIR_TARGET_FD`, anything else is treated as a file name;
  the other operators always take `REDIR_TARGET_FILE`. The target token is
  cloned, unexpanded: the executor expands it at run time (one field, no
  globbing).
- For `G_IO_HERE` the type is `REDIR_FROM_BUFFER` (`<<`) or
  `REDIR_FROM_BUFFER_STRIP` (`<<-`), the target kind `REDIR_TARGET_BUFFER`.
  The body comes from the `TOKEN_END_OF_HEREDOC` token the parser matched
  to this operator (`take_heredoc_token()`): `buffer` is the raw body and
  `buffer_needs_expansion` is false when the delimiter was quoted. The
  executor expands an unquoted body as if double-quoted.

`lower_redirect_list()` lowers a whole `G_REDIRECT_LIST`; the callers that
need it (`command` and `function_definition`) currently loop over the list
themselves.

## Things to keep in mind

1. Everything the executor expands (command words, assignment values,
   redirection targets, here-document bodies, the `for` word list, `case`
   subjects and patterns) stays a token in the AST. The lowering does no
   expansion and no quote removal.
2. The tokenizer, not the lowering, decides reserved words, assignment
   words and `IO_NUMBER`s; by the time a tree reaches `lower.c` the token
   types are final.
3. Separators are stored per item, including the last one (`CMD_EXEC_END`).
   The executor reads them to decide whether to wait for a command.
4. The lowering accepts a few historical tree layouts (`case` word
   positions, a function body without the `G_FUNCTION_BODY` wrapper). New
   parser output uses the layouts described first in each section.
