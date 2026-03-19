#ifndef MIGA_NODE_H
#define MIGA_NODE_H

#include <stddef.h>

#include "miga_cppguard.h"
#include "miga_visibility.h"
#include "strlist.h"
#include "miga_string.h"
#include "token.h"

MIGA_EXTERN_C_START

/* ============================================================================
 * AST Node Type Enumeration
 * ============================================================================ */

/**
 * AST node types representing shell grammar constructs.
 * These map to the POSIX shell grammar productions.
 */
typedef enum miga_node_type_t
{
    /* Basic command constructs */
    MIGA_NODE_TYPE_SIMPLE_COMMAND,    // command with arguments and redirections
    MIGA_NODE_TYPE_PIPELINE,          // sequence of commands connected by pipes
    MIGA_NODE_TYPE_AND_OR_LIST,       // commands connected by && or ||
    MIGA_NODE_TYPE_COMMAND_LIST,      // commands separated by ; or & or newlines

    /* Compound commands */
    MIGA_NODE_TYPE_SUBSHELL,          // ( command_list )
    MIGA_NODE_TYPE_BRACE_GROUP,       // { command_list; }
    MIGA_NODE_TYPE_IF_CLAUSE,         // if/then/else/fi
    MIGA_NODE_TYPE_WHILE_CLAUSE,      // while/do/done
    MIGA_NODE_TYPE_UNTIL_CLAUSE,      // until/do/done
    MIGA_NODE_TYPE_FOR_CLAUSE,        // for/in/do/done
    MIGA_NODE_TYPE_CASE_CLAUSE,       // case/in/esac
    MIGA_NODE_TYPE_FUNCTION_DEF,      // name() compound-command [redirections]
    MIGA_NODE_TYPE_REDIRECTED_COMMAND, // a decorator for commands with redirections. It may have multiple redirections.
                           // It can have many different command types as its child.

    /* Auxiliary nodes */
    MIGA_NODE_TYPE_REDIRECTION,        // A single I/O redirection. There are one or more of these per
                            // REDIRECTED_COMMAND.
    MIGA_NODE_TYPE_CASE_ITEM,         // pattern) command_list ;;
    MIGA_NODE_TYPE_FUNCTION_STORED,   // placeholder for function node moved to function store

    MIGA_NODE_TYPE_COUNT
} miga_node_type_t;

/* ============================================================================
 * AST Operator Enumerations
 * ============================================================================ */

/**
 * Pipeline operators
 */
typedef enum miga_pipeline_op_t
{
    MIGA_PIPELINE_OP_NORMAL,      // pipe stdout only
    MIGA_PIPELINE_OP_MERGE_STDERR // pipe stdout + stderr
} miga_pipeline_op_t;

/**
 * And/Or list operators
 */
typedef enum miga_andor_op_t
{
    MIGA_ANDOR_OP_AND, // &&
    MIGA_ANDOR_OP_OR,  // ||
} miga_andor_op_t;

/**
 * Command list separators.
 *
 * N.B. Each command in a command_list has an associated separator,
 * including the last command (which gets MIGA_CMD_EXEC_END). This ensures:
 *   - items.size == separators.len (1:1 correspondence)
 *   - Simpler indexing: separator[i] describes what follows command[i]
 *   - Executor can easily determine if a command should run in background
 *
 * Example: "echo foo; echo bar; echo baz"
 *   Command 0: "echo foo"  -> separator: MIGA_CMD_EXEC_SEQUENTIAL
 *   Command 1: "echo bar"  -> separator: MIGA_CMD_EXEC_SEQUENTIAL
 *   Command 2: "echo baz"  -> separator: MIGA_CMD_EXEC_END (no actual token)
 */
typedef enum miga_cmd_exec_t
{
    MIGA_CMD_EXEC_SEQUENTIAL, // run, wait, then run next
    MIGA_CMD_EXEC_BACKGROUND, // run without waiting
    MIGA_CMD_EXEC_END         // no more commands
} miga_cmd_exec_t;

/* Command separator list structure */
typedef struct miga_cmd_exec_list_t
{
    miga_cmd_exec_t *separators;
    int len;
    int capacity;
} miga_cmd_exec_list_t;

/**
 * Redirection types
 */
typedef enum miga_redir_t
{
    MIGA_REDIR_READ,        // <      open file for reading
    MIGA_REDIR_WRITE,       // >      truncate + write
    MIGA_REDIR_APPEND,      // >>     append
    MIGA_REDIR_READWRITE,   // <>     read/write
    MIGA_REDIR_WRITE_FORCE, // >|     force overwrite (ignore noclobber)

    MIGA_REDIR_FD_DUP_IN,  // <&     duplicate input FD
    MIGA_REDIR_FD_DUP_OUT, // >&     duplicate output FD

    MIGA_REDIR_FROM_BUFFER,      // <<     heredoc, but now it's a buffer
    MIGA_REDIR_FROM_BUFFER_STRIP // <<-    strip leading tabs
} miga_redir_t;

typedef enum miga_redir_target_t
{
    MIGA_REDIR_TARGET_INVALID,  // should never happen
    MIGA_REDIR_TARGET_FILE,    // target is a filename
    MIGA_REDIR_TARGET_FD,       // target is a numeric fd
    MIGA_REDIR_TARGET_CLOSE,   // target is '-', indicating close fd
    MIGA_REDIR_TARGET_FD_STRING, // io_location is a string (e.g., <&var). Probably UNUSED
    MIGA_REDIR_TARGET_BUFFER // buffer (heredoc)
} miga_redir_target_t;

typedef enum miga_case_action_t
{
    MIGA_CASE_ACTION_NONE,
    MIGA_CASE_ACTION_BREAK,
    MIGA_CASE_ACTION_FALLTHROUGH
} miga_case_action_t;

/* ============================================================================
 * AST Node List Structure
 * ============================================================================ */

/**
 * Dynamic array of AST nodes
 */
typedef struct miga_node_list_t
{
    struct miga_node_t **nodes;
    int size;
    int capacity;
} miga_node_list_t;

/* ============================================================================
 * AST Node Structure
 * ============================================================================ */

/**
 * Generic AST node structure.
 * The interpretation of fields depends on the node type.
 */
typedef struct miga_node_t
{
    miga_node_type_t type;

    /* Location tracking from source tokens */
    int first_line;
    int first_column;
    int last_line;
    int last_column;

    /* Common fields used by different node types */
    union
    {
        /* MIGA_NODE_TYPE_SIMPLE_COMMAND */
        struct
        {
            token_list_t *words;           // command name and arguments
            miga_node_list_t *redirections; // redirections for this command
                                           // NOTE: simple commands's redirections apply only to this command,
                                           // while MIGA_NODE_TYPE_REDIRECTED_COMMAND's redirections apply to the entire command or compound.
            token_list_t *assignments;     // variable assignments (name=value)
        } simple_command;

        /* MIGA_NODE_TYPE_PIPELINE */
        struct
        {
            miga_node_list_t *commands; // list of commands in the pipeline
            bool is_negated;           // true if pipeline starts with !
        } pipeline;

        /* MIGA_NODE_TYPE_AND_OR_LIST */
        struct
        {
            miga_node_t *left;
            miga_node_t *right;
            miga_andor_op_t op; // && or ||
        } andor_list;

        /* MIGA_NODE_TYPE_COMMAND_LIST */
        struct
        {
            miga_node_list_t *items;           // list of commands/pipelines
            miga_cmd_exec_list_t *separators; // separator after each item
        } command_list;

        /* MIGA_NODE_TYPE_SUBSHELL, MIGA_NODE_TYPE_BRACE_GROUP */
        struct
        {
            miga_node_t *body; // command list inside ( ) or { }
        } compound;

        /* MIGA_NODE_TYPE_IF_CLAUSE */
        struct
        {
            miga_node_t *condition;      // condition to test
            miga_node_t *then_body;      // commands if condition is true
            miga_node_list_t *elif_list; // list of elif nodes (each is an AST_ELIF_CLAUSE)
            miga_node_t *else_body;      // commands if all conditions are false (optional)
        } if_clause;

        /* MIGA_NODE_TYPE_WHILE_CLAUSE, MIGA_NODE_TYPE_UNTIL_CLAUSE */
        struct
        {
            miga_node_t *condition; // condition to test
            miga_node_t *body;      // commands to execute in loop
        } loop_clause;

        /* MIGA_NODE_TYPE_FOR_CLAUSE */
        struct
        {
            miga_string_t *variable; // loop variable name
            token_list_t *words; // words to iterate over (can be NULL for "$@")
            miga_node_t *body;   // commands to execute in loop
        } for_clause;

        /* MIGA_NODE_TYPE_CASE_CLAUSE */
        struct
        {
            token_t *word;              // word to match
            miga_node_list_t *case_items; // list of case items
        } case_clause;

        /* MIGA_NODE_TYPE_CASE_ITEM */
        struct
        {
            token_list_t *patterns; // list of patterns
            miga_node_t *body;       // commands to execute if pattern matches
            miga_case_action_t action; // ;;, ;&, or none
        } case_item;

        /* MIGA_NODE_TYPE_FUNCTION_DEF */
        struct
        {
            miga_string_t *name;                // function name
            miga_node_t *body;              // compound command (function body)
            miga_node_list_t *redirections; // optional redirections
        } function_def;

        /* MIGA_NODE_TYPE_REDIRECTED_COMMAND */
        struct
        {
            miga_node_t *command;           // wrapped command (compound/simple/function/etc.)
            miga_node_list_t *redirections; // trailing redirections applied to the command
        } redirected_command;

        /* MIGA_NODE_TYPE_REDIRECTION */
        struct
        {
            miga_redir_t redir_type;
            int io_number;                // fd being redirected (or -1)
            miga_redir_target_t operand; // operand type
#ifndef FUTURE
            bool buffer_needs_expansion; // for BUFFER type: whether to expand content
            miga_string_t *fd_string;     // used only when operand == MIGA_REDIR_TARGET_FD_STRING
            token_t *target;           // used when operand == FILENAME or FD
            miga_string_t *buffer;        // used when operand == BUFFER (heredoc content)
#else
            redir_payload_t payload_type;
            union {
                miga_string_t *fd_string;     // used only when operand == MIGA_REDIR_TARGET_FD_STRING
                token_t *target;           // used when operand == FILENAME or FD
                struct
                {
                    bool needs_expansion; // for BUFFER type: whether to expand content
                    miga_string_t *buffer;     // used when operand == BUFFER (heredoc content)
                } buffer;
            } data;
#endif
        } redirection;
    } data;
} miga_node_t;


/* ============================================================================
 * AST Node Lifecycle Functions
 * ============================================================================ */

/**
 * Create a new AST node of the specified type.
 * Returns NULL on allocation failure.
 */
miga_node_t *miga_node_create(miga_node_type_t type);

miga_node_t *miga_node_clone(const miga_node_t *node);

/**
 * Create a placeholder node indicating a function was moved to the function store.
 * This is used to replace function definition nodes after ownership transfer.
 * Returns NULL on allocation failure.
 */
miga_node_t *miga_create_function_stored_node(void);

/**
 * Destroy an AST node and free all associated memory.
 * Safe to call with NULL.
 * Recursively destroys child nodes.
 */
void miga_node_destroy(miga_node_t **node);

/* ============================================================================
 * AST Node Accessors
 * ============================================================================ */

/**
 * Get the type of an AST node.
 */
miga_node_type_t miga_node_get_type(const miga_node_t *node);

/**
 * Set location information for an AST node.
 */
void miga_node_set_location(miga_node_t *node, int first_line, int first_column,
                          int last_line,
                           int last_column);

void miga_command_list_node_append_item(miga_node_t *node, miga_node_t *item);

void miga_command_list_node_append_separator(miga_node_t *node, miga_cmd_exec_t separator);

void miga_redirection_node_set_buffer_content(miga_node_t *node, const miga_string_t *content);

miga_redir_t miga_redirection_node_get_redir_type(const miga_node_t *node);

const char *miga_redir_to_cstr(miga_redir_t type);

const token_list_t *miga_simple_command_node_get_words(const miga_node_t *node);
strlist_t *miga_simple_command_node_get_word_strlist(const miga_node_t *node);
bool miga_simple_command_node_has_redirections(const miga_node_t *node);
const miga_node_list_t *miga_simple_command_node_get_redirections(const miga_node_t *node);

/* ============================================================================
 * AST Node Creation Helpers
 * ============================================================================ */

/*
 * OWNERSHIP POLICY FOR TOKENS IN AST:
 *
 * The AST takes FULL OWNERSHIP of all token_t pointers and token_list_t
 * structures passed to ast_create_* functions. This includes:
 *   - Individual token_t* (e.g., for case_clause.word, redirection.target)
 *   - token_list_t* (e.g., for simple_command.words, for_clause.words)
 *
 * When an AST node is destroyed via miga_node_destroy():
 *   - All token_t objects are destroyed via token_destroy()
 *   - All token_list_t structures are destroyed via token_list_destroy()
 *   - This recursively destroys all tokens within the lists
 *
 * The caller must NOT:
 *   - Destroy tokens after passing them to AST
 *   - Keep references to tokens after passing them to AST
 *   - Use token_list_destroy() on lists after passing them to AST
 *
 * The caller should:
 *   - Call token_list_release_tokens() on the original token_list from
 *     the parser to clear pointers without destroying tokens
 *   - Then free the token_list structure itself
 */

/**
 * Create a simple command node.
 * OWNERSHIP: Takes ownership of words, redirections, and assignments.
 */
miga_node_t *miga_create_simple_command_node(token_list_t *words,
                                     miga_node_list_t *redirections,
                                     token_list_t *assignments);

/**
 * Create a pipeline node.
 */
miga_node_t *miga_create_pipeline_node(miga_node_list_t *commands, bool is_negated);

/**
 * Create an and/or list node.
 */
miga_node_t *miga_create_andor_list_node(miga_node_t *left, miga_node_t *right,
                                 miga_andor_op_t op);

/**
 * Create a command list node.
 */
miga_node_t *miga_create_command_list_node(void);

/**
 * Create a subshell node.
 */
miga_node_t *miga_create_subshell_node(miga_node_t *body);

/**
 * Create a brace group node.
 */
miga_node_t *miga_create_brace_group_node(miga_node_t *body);

/**
 * Create an if clause node.
 */
miga_node_t *miga_create_if_clause_node(miga_node_t *condition, miga_node_t *then_body);

/**
 * Create a while clause node.
 */
miga_node_t *miga_create_while_clause_node(miga_node_t *condition, miga_node_t *body);

/**
 * Create an until clause node.
 */
miga_node_t *miga_create_until_clause_node(miga_node_t *condition, miga_node_t *body);

/**
 * Create a for clause node.
 * OWNERSHIP: Takes ownership of words token list (clones variable string).
 */
miga_node_t *miga_create_for_clause_node(const miga_string_t *variable, token_list_t *words,
                                 miga_node_t *body);

/**
 * Create a case clause node.
 * OWNERSHIP: Takes ownership of word token.
 */
miga_node_t *miga_create_case_clause_node(token_t *word);

/**
 * Create a case item node.
 * OWNERSHIP: Takes ownership of patterns token list.
 */
miga_node_t *miga_create_case_item_node(token_list_t *patterns, miga_node_t *body);

/**
 * Create a function definition node.
 */
miga_node_t *miga_create_function_def_node(const miga_string_t *name, miga_node_t *body,
                                   miga_node_list_t *redirections);

/**
 * Create a redirected command wrapper node.
 */
miga_node_t *miga_create_redirected_command_node(miga_node_t *command, miga_node_list_t *redirections);

/**
 * Create a redirection node.
 * OWNERSHIP: Takes ownership of target token.
 */
miga_node_t *miga_create_redirection_node(miga_redir_t redir_type,
                                   miga_redir_target_t operand, int io_number,
                                  miga_string_t *fd_string, token_t *target);

/* ============================================================================
 * AST Node List Functions
 * ============================================================================ */

/**
 * Create a new AST node list.
 * Returns NULL on allocation failure.
 */
miga_node_list_t *miga_create_node_list(void);

// Deep clone an AST node list
miga_node_list_t *miga_node_list_clone(const miga_node_list_t *other);

/**
 * Destroy an AST node list and all contained nodes.
 * Safe to call with NULL.
 */
void miga_node_list_destroy(miga_node_list_t **list);

/**
 * Append an AST node to a list.
 * The list takes ownership of the node.
 * Returns 0 on success, -1 on failure.
 */
int miga_node_list_append(miga_node_list_t *list, miga_node_t *node);

/**
 * Get the number of nodes in a list.
 */
int miga_node_list_size(const miga_node_list_t *list);

/**
 * Get a node by index.
 * Returns NULL if index is out of bounds.
 */
miga_node_t *miga_node_list_get(const miga_node_list_t *list, int index);

/* ============================================================================
 * Command List Separator Access Functions
 * ============================================================================ */

/**
 * Check if a command list node has any separators.
 * INVARIANT: For command lists, separator_count() == item_count()
 */
bool miga_command_list_node_has_separators(const miga_node_t *node);

/**
 * Get the number of separators in a command list.
 * INVARIANT: Returns the same value as miga_node_list_size() for the items.
 */
int miga_command_list_node_separator_count(const miga_node_t *node);

/**
 * Get a separator by index.
 * The separator at index i describes what follows command i.
 * The last command will have MIGA_CMD_EXEC_END if no separator token was present.
 */
miga_cmd_exec_t miga_command_list_node_get_separator(const miga_node_t *node, int index);

/* ============================================================================
 * AST Utility Functions
 * ============================================================================ */

/**
 * Convert an AST node type to a human-readable string.
 */
const char *miga_node_type_to_cstr(miga_node_type_t type);

/**
 * Create a debug string representation of an AST node.
 * Caller is responsible for freeing the returned string.
 */
miga_string_t *miga_node_to_string(const miga_node_t *node);

/**
 * Create a command line string representation of an AST node.
 * This reconstructs the command line as closely as possible.
 * Caller is responsible for freeing the returned string.
 */
miga_string_t *miga_node_to_command_line_full(const miga_node_t *node);

/**
 * Create a debug string representation of an AST (tree format).
 * Caller is responsible for freeing the returned string.
 */
miga_string_t *miga_node_tree_to_string(const miga_node_t *root);

void miga_node_print(const miga_node_t *root);

/* ============================================================================
 * Command Separator List Functions
 * ============================================================================ */
miga_cmd_exec_list_t *miga_cmd_exec_list_create(void);

// Deep clone a command separator list
miga_cmd_exec_list_t *miga_cmd_exec_list_clone(const miga_cmd_exec_list_t *other);

void miga_cmd_exec_list_destroy(miga_cmd_exec_list_t **lst);
void miga_cmd_exec_list_add(miga_cmd_exec_list_t *list, miga_cmd_exec_t sep);
miga_cmd_exec_t miga_cmd_exec_list_get(const miga_cmd_exec_list_t *list, int index);

MIGA_EXTERN_C_END

#endif /* MIGA_NODE_H */
