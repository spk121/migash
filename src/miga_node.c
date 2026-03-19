#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miga_node.h"

#include "miga_log.h"
#include "strlist.h"
#include "miga_string.h"
#include "token.h"
#include "miga_alloc.h"



/* ============================================================================
 * Constants
 * ============================================================================ */

#define INITIAL_LIST_CAPACITY 8

/* ============================================================================
 * AST Node Lifecycle Functions
 * ============================================================================ */

miga_node_t *miga_node_create(miga_node_type_t type)
{
    MIGA_EXPECTS(type >= 0 && type < MIGA_NODE_TYPE_COUNT);

    miga_node_t *node = miga_calloc(1, sizeof(miga_node_t));
    node->type = type;
    return node;
}

miga_node_t *miga_node_clone(const miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(node);

    miga_node_t *new_node = miga_node_create(node->type);
    if (!new_node)
        return NULL;

    // This is a deep copy, because by policy no AST node can have more than 1 owner.
    memcpy(new_node, node, sizeof(miga_node_t));
    switch (node->type)
    {
        // These node types contain pointers to owned data that need to be cloned.
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        new_node->data.simple_command.words = token_list_clone(node->data.simple_command.words);
        new_node->data.simple_command.redirections =
            miga_node_list_clone(node->data.simple_command.redirections);
        new_node->data.simple_command.assignments =
            token_list_clone(node->data.simple_command.assignments);
        break;
    case MIGA_NODE_TYPE_PIPELINE:
        new_node->data.pipeline.commands =
            miga_node_list_clone(node->data.pipeline.commands);
        break;
    case MIGA_NODE_TYPE_AND_OR_LIST:
        new_node->data.andor_list.left = miga_node_clone(node->data.andor_list.left);
        new_node->data.andor_list.right = miga_node_clone(node->data.andor_list.right);
        break;
    case MIGA_NODE_TYPE_COMMAND_LIST:
        new_node->data.command_list.items =
            miga_node_list_clone(node->data.command_list.items);
        new_node->data.command_list.separators =
            miga_cmd_exec_list_clone(node->data.command_list.separators);
        break;
    case MIGA_NODE_TYPE_SUBSHELL:
    case MIGA_NODE_TYPE_BRACE_GROUP:
        new_node->data.compound.body = miga_node_clone(node->data.compound.body);
        break;
    case MIGA_NODE_TYPE_IF_CLAUSE:
        new_node->data.if_clause.condition = miga_node_clone(node->data.if_clause.condition);
        new_node->data.if_clause.then_body = miga_node_clone(node->data.if_clause.then_body);
        if (node->data.if_clause.elif_list)
            new_node->data.if_clause.elif_list = miga_node_list_clone(node->data.if_clause.elif_list);
        if (node->data.if_clause.else_body)
            new_node->data.if_clause.else_body = miga_node_clone(node->data.if_clause.else_body);
        break;
    case MIGA_NODE_TYPE_WHILE_CLAUSE:
    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        new_node->data.loop_clause.condition = miga_node_clone(node->data.loop_clause.condition);
        new_node->data.loop_clause.body = miga_node_clone(node->data.loop_clause.body);
        break;
    case MIGA_NODE_TYPE_FOR_CLAUSE:
        new_node->data.for_clause.variable = string_create_from(node->data.for_clause.variable);
        new_node->data.for_clause.words = token_list_clone(node->data.for_clause.words);
        new_node->data.for_clause.body = miga_node_clone(node->data.for_clause.body);
        break;
    case MIGA_NODE_TYPE_CASE_CLAUSE:
        new_node->data.case_clause.word = token_clone(node->data.case_clause.word);
        new_node->data.case_clause.case_items =
            miga_node_list_clone(node->data.case_clause.case_items);
        break;
    case MIGA_NODE_TYPE_CASE_ITEM:
        new_node->data.case_item.patterns = token_list_clone(node->data.case_item.patterns);
        new_node->data.case_item.body = miga_node_clone(node->data.case_item.body);
        break;
    case MIGA_NODE_TYPE_FUNCTION_DEF:
        new_node->data.function_def.name = string_create_from(node->data.function_def.name);
        new_node->data.function_def.body = miga_node_clone(node->data.function_def.body);
        new_node->data.function_def.redirections =
            miga_node_list_clone(node->data.function_def.redirections);
        break;
    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        new_node->data.redirected_command.command =
            miga_node_clone(node->data.redirected_command.command);
        new_node->data.redirected_command.redirections =
            miga_node_list_clone(node->data.redirected_command.redirections);
        break;
    case MIGA_NODE_TYPE_REDIRECTION:
        switch (node->data.redirection.operand)
        {
        case MIGA_REDIR_TARGET_FILE:
        case MIGA_REDIR_TARGET_FD:
            new_node->data.redirection.target = token_clone(node->data.redirection.target);
            break;
        case MIGA_REDIR_TARGET_FD_STRING:
            new_node->data.redirection.fd_string =
                string_create_from(node->data.redirection.fd_string);
            break;
        case MIGA_REDIR_TARGET_BUFFER:
            new_node->data.redirection.target = token_clone(node->data.redirection.target);
            new_node->data.redirection.buffer =
                string_create_from(node->data.redirection.buffer);
            break;
        case MIGA_REDIR_TARGET_CLOSE:
        case MIGA_REDIR_TARGET_INVALID:
        default:
            /* no operand storage */
            break;
        }
        break;
    case MIGA_NODE_TYPE_FUNCTION_STORED:
        // No owned data to clone.
        break;
    case MIGA_NODE_TYPE_COUNT:
    default:
        // Other node types do not own any heap data; shallow copy is sufficient.
        break;
    }

    return new_node;
}

void miga_node_destroy(miga_node_t **node)
{
    if (!node) return;
    miga_node_t *n = *node;

    if (n == NULL)
        return;

    switch (n->type)
    {
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        if (n->data.simple_command.words != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.simple_command.words);
        }
        if (n->data.simple_command.redirections != NULL)
        {
            miga_node_list_destroy(&n->data.simple_command.redirections);
        }
        if (n->data.simple_command.assignments != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.simple_command.assignments);
        }
        break;

    case MIGA_NODE_TYPE_PIPELINE:
        if (n->data.pipeline.commands != NULL)
        {
            miga_node_list_destroy(&n->data.pipeline.commands);
        }
        break;

    case MIGA_NODE_TYPE_AND_OR_LIST:
        miga_node_destroy(&n->data.andor_list.left);
        miga_node_destroy(&n->data.andor_list.right);
        break;

    case MIGA_NODE_TYPE_COMMAND_LIST:
        miga_node_list_destroy(&n->data.command_list.items);
        miga_cmd_exec_list_destroy(&n->data.command_list.separators);

        break;

    case MIGA_NODE_TYPE_SUBSHELL:
    case MIGA_NODE_TYPE_BRACE_GROUP:
        miga_node_destroy(&n->data.compound.body);
        break;

    case MIGA_NODE_TYPE_IF_CLAUSE:
        miga_node_destroy(&n->data.if_clause.condition);
        miga_node_destroy(&n->data.if_clause.then_body);
        if (n->data.if_clause.elif_list != NULL)
        {
            miga_node_list_destroy(&n->data.if_clause.elif_list);
        }
        miga_node_destroy(&n->data.if_clause.else_body);
        break;

    case MIGA_NODE_TYPE_WHILE_CLAUSE:
    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        miga_node_destroy(&n->data.loop_clause.condition);
        miga_node_destroy(&n->data.loop_clause.body);
        break;

    case MIGA_NODE_TYPE_FOR_CLAUSE:
        if (n->data.for_clause.variable != NULL)
        {
            string_destroy(&n->data.for_clause.variable);
        }
        if (n->data.for_clause.words != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.for_clause.words);
        }
        miga_node_destroy(&n->data.for_clause.body);
        break;

    case MIGA_NODE_TYPE_CASE_CLAUSE:
        if (n->data.case_clause.word != NULL)
        {
            // AST owns this token - destroy it
            token_destroy(&n->data.case_clause.word);
        }
        if (n->data.case_clause.case_items != NULL)
        {
            miga_node_list_destroy(&n->data.case_clause.case_items);
        }
        break;

    case MIGA_NODE_TYPE_CASE_ITEM:
        if (n->data.case_item.patterns != NULL)
        {
            // AST owns these tokens - destroy them
            token_list_destroy(&n->data.case_item.patterns);
        }
        miga_node_destroy(&n->data.case_item.body);
        break;

    case MIGA_NODE_TYPE_FUNCTION_DEF:
        if (n->data.function_def.name != NULL)
        {
            string_destroy(&n->data.function_def.name);
        }
        miga_node_destroy(&n->data.function_def.body);
        if (n->data.function_def.redirections != NULL)
        {
            miga_node_list_destroy(&n->data.function_def.redirections);
        }
        break;

    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        miga_node_destroy(&n->data.redirected_command.command);
        if (n->data.redirected_command.redirections != NULL)
        {
            miga_node_list_destroy(&n->data.redirected_command.redirections);
        }
        break;

    case MIGA_NODE_TYPE_REDIRECTION: {
        switch (n->data.redirection.operand)
        {
        case MIGA_REDIR_TARGET_FILE:
        case MIGA_REDIR_TARGET_FD:
            if (n->data.redirection.target)
                token_destroy(&n->data.redirection.target);
            break;

        case MIGA_REDIR_TARGET_FD_STRING:
            if (n->data.redirection.fd_string)
                string_destroy(&n->data.redirection.fd_string);
            break;

        case MIGA_REDIR_TARGET_BUFFER:
            if (n->data.redirection.target)
                token_destroy(&n->data.redirection.target);
            if (n->data.redirection.buffer)
                string_destroy(&n->data.redirection.buffer);
            break;

        case MIGA_REDIR_TARGET_CLOSE:
        case MIGA_REDIR_TARGET_INVALID:
        default:
            /* no operand storage */
            break;
        }
    }
    break;

    case MIGA_NODE_TYPE_FUNCTION_STORED:
        // No owned data to destroy.
        break;
    case MIGA_NODE_TYPE_COUNT:
    default:
        break;
    }

    xfree(n);
    *node = NULL;
}

/* ============================================================================
 * AST Node Accessors
 * ============================================================================ */

miga_node_type_t miga_node_get_type(const miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    return node->type;
}

void miga_node_set_location(miga_node_t *node, int first_line, int first_column,
                          int last_line, int last_column)
{
    MIGA_EXPECTS_NOT_NULL(node);
    node->first_line = first_line;
    node->first_column = first_column;
    node->last_line = last_line;
    node->last_column = last_column;
}

void miga_command_list_node_append_item(miga_node_t *node, miga_node_t *item)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(node->type, MIGA_NODE_TYPE_COMMAND_LIST);
    MIGA_EXPECTS_NOT_NULL(node->data.command_list.items);
    MIGA_EXPECTS_NOT_NULL(item);

    miga_node_list_append(node->data.command_list.items, item);
}

void miga_command_list_node_append_separator(miga_node_t *node, miga_cmd_exec_t separator)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(node->type, MIGA_NODE_TYPE_COMMAND_LIST);
    MIGA_EXPECTS_NOT_NULL(node->data.command_list.separators);

    miga_cmd_exec_list_add(node->data.command_list.separators, separator);
}

miga_redir_t miga_redirection_node_get_redir_type(const miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS(node->type == MIGA_NODE_TYPE_REDIRECTION);

    return node->data.redirection.redir_type;
}

void miga_redirection_node_set_buffer_content(miga_node_t *node, const miga_string_t *content)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS(node->type == MIGA_NODE_TYPE_REDIRECTION);
    MIGA_EXPECTS_NOT_NULL(content);

    if (node->data.redirection.buffer != NULL)
    {
        string_destroy(&node->data.redirection.buffer);
    }
    node->data.redirection.buffer = string_create_from(content);
}

/* ============================================================================
 * AST Node Creation Helpers
 * ============================================================================ */

miga_node_t *miga_create_function_stored_node(void)
{
    return miga_node_create(MIGA_NODE_TYPE_FUNCTION_STORED);
}

miga_node_t *miga_create_simple_command_node(token_list_t *words,
                                     miga_node_list_t *redirections,
                                     token_list_t *assignments)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_SIMPLE_COMMAND);
    node->data.simple_command.words = words;
    node->data.simple_command.redirections = redirections;
    node->data.simple_command.assignments = assignments;
    return node;
}

miga_node_t *miga_create_pipeline_node(miga_node_list_t *commands, bool is_negated)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_PIPELINE);
    node->data.pipeline.commands = commands;
    node->data.pipeline.is_negated = is_negated;
    return node;
}

miga_node_t *miga_create_andor_list_node(miga_node_t *left, miga_node_t *right,
                                 miga_andor_op_t op)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_AND_OR_LIST);
    node->data.andor_list.left = left;
    node->data.andor_list.right = right;
    node->data.andor_list.op = op;
    return node;
}

miga_node_t *miga_create_command_list_node(void)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_COMMAND_LIST);
    node->data.command_list.items = miga_create_node_list();
    node->data.command_list.separators = miga_cmd_exec_list_create(); // Allocate here
    return node;
}

miga_node_t *miga_create_subshell_node(miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_SUBSHELL);
    node->data.compound.body = body;
    return node;
}

miga_node_t *miga_create_brace_group_node(miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_BRACE_GROUP);
    node->data.compound.body = body;
    return node;
}

miga_node_t *miga_create_if_clause_node(miga_node_t *condition, miga_node_t *then_body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_IF_CLAUSE);
    node->data.if_clause.condition = condition;
    node->data.if_clause.then_body = then_body;
    node->data.if_clause.elif_list = NULL;
    node->data.if_clause.else_body = NULL;
    return node;
}

miga_node_t *miga_create_while_clause_node(miga_node_t *condition, miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_WHILE_CLAUSE);
    node->data.loop_clause.condition = condition;
    node->data.loop_clause.body = body;
    return node;
}

miga_node_t *miga_create_until_clause_node(miga_node_t *condition, miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_UNTIL_CLAUSE);
    node->data.loop_clause.condition = condition;
    node->data.loop_clause.body = body;
    return node;
}

miga_node_t *miga_create_for_clause_node(const miga_string_t *variable, token_list_t *words,
                                 miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_FOR_CLAUSE);
    node->data.for_clause.variable = string_create_from(variable);
    node->data.for_clause.words = words;
    node->data.for_clause.body = body;
    return node;
}

miga_node_t *miga_create_case_clause_node(token_t *word)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_CASE_CLAUSE);
    node->data.case_clause.word = word;
    node->data.case_clause.case_items = miga_create_node_list();
    return node;
}

miga_node_t *miga_create_case_item_node(token_list_t *patterns, miga_node_t *body)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_CASE_ITEM);
    node->data.case_item.patterns = patterns;
    node->data.case_item.body = body;
    return node;
}

miga_node_t *miga_create_function_def_node(const miga_string_t *name, miga_node_t *body,
                                   miga_node_list_t *redirections)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_FUNCTION_DEF);
    node->data.function_def.name = string_create_from(name);
    node->data.function_def.body = body;
    node->data.function_def.redirections = redirections;
    return node;
}

miga_node_t *miga_create_redirection_node(miga_redir_t redir_type,
                                   miga_redir_target_t operand, int io_number,
                                  miga_string_t *fd_string, token_t *target)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_REDIRECTION);
    node->data.redirection.redir_type = redir_type;
    node->data.redirection.operand = operand;
    node->data.redirection.io_number = io_number;
    node->data.redirection.fd_string = fd_string;
    node->data.redirection.target = target;
    node->data.redirection.buffer = NULL;
    return node;
}

miga_node_t *miga_create_redirected_command_node(miga_node_t *command, miga_node_list_t *redirections)
{
    miga_node_t *node = miga_node_create(MIGA_NODE_TYPE_REDIRECTED_COMMAND);
    node->data.redirected_command.command = command;
    node->data.redirected_command.redirections = redirections;
    return node;
}

/* ============================================================================
 * AST Node List Functions
 * ============================================================================ */

miga_node_list_t *miga_create_node_list(void)
{
    miga_node_list_t *list = (miga_node_list_t *)miga_calloc(1, sizeof(miga_node_list_t));
    list->nodes = (miga_node_t **)miga_calloc(INITIAL_LIST_CAPACITY, sizeof(miga_node_t *));
    list->size = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    return list;
}

miga_node_list_t *miga_node_list_clone(const miga_node_list_t *other)
{
    MIGA_EXPECTS_NOT_NULL(other);

    miga_node_list_t *new_list = miga_create_node_list();
    if (!new_list)
        return NULL;

    for (int i = 0; i < other->size; i++)
    {
        miga_node_t *cloned_node = miga_node_clone(other->nodes[i]);
        if (!cloned_node)
        {
            miga_node_list_destroy(&new_list);
            return NULL;
        }
        miga_node_list_append(new_list, cloned_node);
    }

    return new_list;
}

void miga_node_list_destroy(miga_node_list_t **list)
{
    if (!list) return;
    miga_node_list_t *l = *list;

    if (l == NULL)
        return;

    for (int i = 0; i < l->size; i++)
    {
        miga_node_destroy(&l->nodes[i]);
    }

    xfree(l->nodes);
    xfree(l);
    *list = NULL;
}

int miga_node_list_append(miga_node_list_t *list, miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(list);
    MIGA_EXPECTS_NOT_NULL(node);

    if (list->size >= list->capacity)
    {
        int new_capacity = list->capacity * 2;
        miga_node_t **new_nodes = (miga_node_t **)xrealloc(list->nodes,
                                                         new_capacity * sizeof(miga_node_t *));
        list->nodes = new_nodes;
        list->capacity = new_capacity;
    }

    list->nodes[list->size++] = node;
    return 0;
}

int miga_node_list_size(const miga_node_list_t *list)
{
    MIGA_EXPECTS_NOT_NULL(list);
    return list->size;
}

miga_node_t *miga_node_list_get(const miga_node_list_t *list, int index)
{
    MIGA_EXPECTS_NOT_NULL(list);
    MIGA_EXPECTS(index >= 0);
    Expects_lt(index, list->size);
    return list->nodes[index];
}

bool miga_command_list_node_has_separators(const miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    if (node->type != MIGA_NODE_TYPE_COMMAND_LIST)
    {
        return false;
    }
    return node->data.command_list.separators != NULL &&
           node->data.command_list.separators->len > 0;
}

int miga_command_list_node_separator_count(const miga_node_t *node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(node->type, MIGA_NODE_TYPE_COMMAND_LIST);
    if (node->data.command_list.separators == NULL)
    {
        return 0;
    }
    return node->data.command_list.separators->len;
}

miga_cmd_exec_t miga_command_list_node_get_separator(const miga_node_t *node, int index)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(node->type, MIGA_NODE_TYPE_COMMAND_LIST);
    Expects_lt(index, miga_command_list_node_separator_count(node));
    return node->data.command_list.separators->separators[index];
}

/* ============================================================================
 * AST Utility Functions
 * ============================================================================ */

const char *miga_node_type_to_cstr(miga_node_type_t type)
{
    switch (type)
    {
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        return "SIMPLE_COMMAND";
    case MIGA_NODE_TYPE_PIPELINE:
        return "PIPELINE";
    case MIGA_NODE_TYPE_AND_OR_LIST:
        return "AND_OR_LIST";
    case MIGA_NODE_TYPE_COMMAND_LIST:
        return "COMMAND_LIST";
    case MIGA_NODE_TYPE_SUBSHELL:
        return "SUBSHELL";
    case MIGA_NODE_TYPE_BRACE_GROUP:
        return "BRACE_GROUP";
    case MIGA_NODE_TYPE_IF_CLAUSE:
        return "IF_CLAUSE";
    case MIGA_NODE_TYPE_WHILE_CLAUSE:
        return "WHILE_CLAUSE";
    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        return "UNTIL_CLAUSE";
    case MIGA_NODE_TYPE_FOR_CLAUSE:
        return "FOR_CLAUSE";
    case MIGA_NODE_TYPE_CASE_CLAUSE:
        return "CASE_CLAUSE";
    case MIGA_NODE_TYPE_FUNCTION_DEF:
        return "FUNCTION_DEF";
    case MIGA_NODE_TYPE_REDIRECTION:
        return "REDIRECTION";
    case MIGA_NODE_TYPE_CASE_ITEM:
        return "CASE_ITEM";
    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        return "REDIRECTED_COMMAND";
    case MIGA_NODE_TYPE_FUNCTION_STORED:
        return "FUNCTION_STORED";
    case MIGA_NODE_TYPE_COUNT:
    default:
        return "UNKNOWN";
    }
}

const char *miga_redir_to_cstr(miga_redir_t type)
{
    switch (type)
    {
    case MIGA_REDIR_READ:
        return "<";
    case MIGA_REDIR_WRITE:
        return ">";
    case MIGA_REDIR_APPEND:
        return ">>";
    case MIGA_REDIR_FROM_BUFFER:
        return "<<";
    case MIGA_REDIR_FROM_BUFFER_STRIP:
        return "<<-";
    case MIGA_REDIR_FD_DUP_IN:
        return "<&";
    case MIGA_REDIR_FD_DUP_OUT:
        return ">&";
    case MIGA_REDIR_READWRITE:
        return "<>";
    case MIGA_REDIR_WRITE_FORCE:
        return ">|";
    default:
        return "UNKNOWN";
    }
}

const token_list_t* miga_simple_command_node_get_words(const miga_node_t* node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(miga_node_get_type(node), MIGA_NODE_TYPE_SIMPLE_COMMAND);

    return (const token_list_t *) node->data.simple_command.words;
}

strlist_t* miga_simple_command_node_get_word_strlist(const miga_node_t* node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(miga_node_get_type(node), MIGA_NODE_TYPE_SIMPLE_COMMAND);

    token_list_t *words = node->data.simple_command.words;
    int len = token_list_size(words);
    strlist_t *sl = strlist_create();
    for (int i = 0; i < len; i ++)
    {
        miga_string_t *txt = token_get_all_text(token_list_get(words, i));
        strlist_move_push_back(sl, &txt);
    }
    return sl;
}

bool miga_simple_command_node_has_redirections(const miga_node_t* node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(miga_node_get_type(node), MIGA_NODE_TYPE_SIMPLE_COMMAND);
    miga_node_list_t *redirs = node->data.simple_command.redirections;
    if (redirs && miga_node_list_size(redirs) > 0)
        return true;
    return false;
}

const miga_node_list_t* miga_simple_command_node_get_redirections(const miga_node_t* node)
{
    MIGA_EXPECTS_NOT_NULL(node);
    MIGA_EXPECTS_EQ(miga_node_get_type(node), MIGA_NODE_TYPE_SIMPLE_COMMAND);
    return node->data.simple_command.redirections;
}

static void indent_str(miga_string_t *str, int i)
{
    for (int j = 0; j < i; j++)
    {
        string_append_cstr(str, "  ");
    }
}

static void ast_node_to_string_helper(const miga_node_t *node, miga_string_t *result, int indent_level);

miga_string_t *miga_node_to_string(const miga_node_t *node)
{
    miga_string_t *result = string_create();
    ast_node_to_string_helper(node, result, 0);
    return result;
}

static void ast_node_to_string_helper(const miga_node_t *node, miga_string_t *result, int indent_level)
{
    if (node == NULL)
    {
        return;
    }

    indent_str(result, indent_level);

    string_append_cstr(result, miga_node_type_to_cstr(node->type));
    string_append_cstr(result, "\n");

    // Recursively print children based on node type
    switch (node->type)
    {
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        if (node->data.simple_command.assignments != NULL &&
            node->data.simple_command.assignments->size > 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "assignments: ");
            miga_string_t *assignments_str =
                token_list_to_string(node->data.simple_command.assignments, indent_level + 1);
            string_append(result, assignments_str);
            string_destroy(&assignments_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.simple_command.words != NULL && node->data.simple_command.words->size > 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "words:\n");
            miga_string_t *words_str =
                token_list_to_string(node->data.simple_command.words, indent_level + 1);
            string_append(result, words_str);
            string_destroy(&words_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.simple_command.redirections != NULL &&
            miga_node_list_size(node->data.simple_command.redirections) > 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "redirections:\n");
            for (int i = 0; i < miga_node_list_size(node->data.simple_command.redirections); i++)
            {
                miga_node_t *redir = miga_node_list_get(node->data.simple_command.redirections, i);
                ast_node_to_string_helper(redir, result, indent_level + 2);
            }
        }
        break;

    case MIGA_NODE_TYPE_PIPELINE:
        if (node->data.pipeline.is_negated)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "negated: true\n");
        }
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                ast_node_to_string_helper(node->data.pipeline.commands->nodes[i], result,
                                          indent_level + 1);
            }
        }
        break;

    case MIGA_NODE_TYPE_AND_OR_LIST:
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "op: ");
        string_append_cstr(result, node->data.andor_list.op == MIGA_ANDOR_OP_AND ? "&&" : "||");
        string_append_cstr(result, "\n");
        ast_node_to_string_helper(node->data.andor_list.left, result, indent_level + 1);
        ast_node_to_string_helper(node->data.andor_list.right, result, indent_level + 1);
        break;

    case MIGA_NODE_TYPE_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                ast_node_to_string_helper(node->data.command_list.items->nodes[i], result,
                                          indent_level + 1);

                // Print the separator for this command
                if (node->data.command_list.separators != NULL &&
                    i < node->data.command_list.separators->len)
                {
                    miga_cmd_exec_t sep =
                        miga_cmd_exec_list_get(node->data.command_list.separators, i);
                    for (int j = 0; j < indent_level + 1; j++)
                        string_append_cstr(result, "  ");

                    switch (sep)
                    {
                    case MIGA_CMD_EXEC_SEQUENTIAL:
                        string_append_cstr(result, "separator: ;\n");
                        break;
                    case MIGA_CMD_EXEC_BACKGROUND:
                        string_append_cstr(result, "separator: &\n");
                        break;
                    case MIGA_CMD_EXEC_END:
                        string_append_cstr(result, "separator: EOL\n");
                        break;
                    }
                }
            }
        }
        break;

    case MIGA_NODE_TYPE_SUBSHELL:
    case MIGA_NODE_TYPE_BRACE_GROUP:
        ast_node_to_string_helper(node->data.compound.body, result, indent_level + 1);
        break;

    case MIGA_NODE_TYPE_IF_CLAUSE:
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "condition:\n");
        ast_node_to_string_helper(node->data.if_clause.condition, result, indent_level + 2);
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "then:\n");
        ast_node_to_string_helper(node->data.if_clause.then_body, result, indent_level + 2);
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < miga_node_list_size(node->data.if_clause.elif_list); i++)
            {
                miga_node_t *elif_node = miga_node_list_get(node->data.if_clause.elif_list, i);
                string_append_cstr(result, " ; elif ");
                ast_node_to_string_helper(elif_node->data.if_clause.condition, result,
                                          indent_level + 1);
                string_append_cstr(result, " ; then ");
                ast_node_to_string_helper(elif_node->data.if_clause.then_body, result,
                                          indent_level + 1);
            }
        }

        if (node->data.if_clause.else_body != NULL)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "else:\n");
            ast_node_to_string_helper(node->data.if_clause.else_body, result, indent_level + 2);
        }
        break;

    case MIGA_NODE_TYPE_WHILE_CLAUSE:
    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "condition:\n");
        ast_node_to_string_helper(node->data.loop_clause.condition, result, indent_level + 2);
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "body:\n");
        ast_node_to_string_helper(node->data.loop_clause.body, result, indent_level + 2);
        break;

    case MIGA_NODE_TYPE_FOR_CLAUSE:
        if (node->data.for_clause.variable != NULL)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "variable: ");
            string_append(result, node->data.for_clause.variable);
            string_append_cstr(result, "\n");
        }
        ast_node_to_string_helper(node->data.for_clause.body, result, indent_level + 1);
        break;
    case MIGA_NODE_TYPE_CASE_CLAUSE:
        if (node->data.case_clause.word != NULL)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "word: ");
            miga_string_t *word_str = token_to_string(node->data.case_clause.word);
            string_append(result, word_str);
            string_destroy(&word_str);
            string_append_cstr(result, "\n");
        }
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < miga_node_list_size(node->data.case_clause.case_items); i++)
            {
                miga_node_t *case_item = miga_node_list_get(node->data.case_clause.case_items, i);
                ast_node_to_string_helper(case_item, result, indent_level + 1);
            }
        }
        break;
    case MIGA_NODE_TYPE_CASE_ITEM:
        if (node->data.case_item.patterns != NULL && node->data.case_item.patterns->size > 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "patterns: ");
            miga_string_t *patterns_str =
                token_list_to_string(node->data.case_item.patterns, indent_level + 1);
            string_append(result, patterns_str);
            string_destroy(&patterns_str);
            string_append_cstr(result, "\n");
        }
        ast_node_to_string_helper(node->data.case_item.body, result, indent_level + 1);
        break;
    case MIGA_NODE_TYPE_FUNCTION_DEF:
        if (node->data.function_def.name != NULL)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "name: ");
            string_append(result, node->data.function_def.name);
            string_append_cstr(result, "\n");
        }
        ast_node_to_string_helper(node->data.function_def.body, result, indent_level + 1);
        break;

    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        if (node->data.redirected_command.command != NULL)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "command:\n");
            ast_node_to_string_helper(node->data.redirected_command.command, result,
                                      indent_level + 2);
        }
        if (node->data.redirected_command.redirections != NULL &&
            miga_node_list_size(node->data.redirected_command.redirections) > 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "redirections:\n");
            for (int i = 0; i < miga_node_list_size(node->data.redirected_command.redirections); i++)
            {
                miga_node_t *redir =
                    miga_node_list_get(node->data.redirected_command.redirections, i);
                ast_node_to_string_helper(redir, result, indent_level + 2);
            }
        }
        break;

    case MIGA_NODE_TYPE_REDIRECTION: {
        /* Print redirection operator */
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "type: ");
        string_append_cstr(result, miga_redir_to_cstr(node->data.redirection.redir_type));
        string_append_cstr(result, "\n");

        /* Print io_number if present */
        if (node->data.redirection.io_number >= 0)
        {
            indent_str(result, indent_level + 1);
            string_append_cstr(result, "io_number: ");
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", node->data.redirection.io_number);
            string_append_cstr(result, buf);
            string_append_cstr(result, "\n");
        }

        /* operand kind */
        indent_str(result, indent_level + 1);
        string_append_cstr(result, "operand: ");
        switch (node->data.redirection.operand)
        {
        case MIGA_REDIR_TARGET_INVALID:
            string_append_cstr(result, "INVALID");
            break;
        case MIGA_REDIR_TARGET_FILE:
            string_append_cstr(result, "FILE");
            break;
        case MIGA_REDIR_TARGET_FD:
            string_append_cstr(result, "FD");
            break;
        case MIGA_REDIR_TARGET_FD_STRING:
            string_append_cstr(result, "FD_STRING");
            break;
        case MIGA_REDIR_TARGET_BUFFER:
            string_append_cstr(result, "BUFFER");
            break;
        case MIGA_REDIR_TARGET_CLOSE:
            string_append_cstr(result, "CLOSE");
            break;
        }
        string_append_cstr(result, "\n");

        /* Print operand-specific fields */
        if (node->data.redirection.operand == MIGA_REDIR_TARGET_FILE ||
            node->data.redirection.operand == MIGA_REDIR_TARGET_FD)
        {
            if (node->data.redirection.target != NULL)
            {
                indent_str(result, indent_level + 1);
                string_append_cstr(result, "target: ");
                miga_string_t *target_str = token_to_string(node->data.redirection.target);
                string_append(result, target_str);
                string_destroy(&target_str);
                string_append_cstr(result, "\n");
            }
        }

        if (node->data.redirection.operand == MIGA_REDIR_TARGET_FD_STRING)
        {
            if (node->data.redirection.fd_string != NULL)
            {
                indent_str(result, indent_level + 1);
                string_append_cstr(result, "fd_string: ");
                string_append(result, node->data.redirection.fd_string);
                string_append_cstr(result, "\n");
            }
        }

        if (node->data.redirection.operand == MIGA_REDIR_TARGET_BUFFER)
        {
            if (node->data.redirection.buffer != NULL)
            {
                indent_str(result, indent_level + 1);
                string_append_cstr(result, "buffer: ");
                string_append(result, node->data.redirection.buffer);
                string_append_cstr(result, "\n");
            }
        }
    }
    break;

    case MIGA_NODE_TYPE_FUNCTION_STORED:
        // No additional data to print.
        break;
    case MIGA_NODE_TYPE_COUNT:
    default:
        break;
    }
}

miga_string_t *miga_node_tree_to_string(const miga_node_t *root)
{
    return miga_node_to_string(root);
}

void miga_node_print(const miga_node_t *root)
{
    miga_string_t *str = miga_node_tree_to_string(root);
    puts(string_cstr(str));
    string_destroy(&str);
}

static void ast_node_to_command_line_full_helper(const miga_node_t *node, miga_string_t *result,
                                                 int level)
{
    if (node == NULL)
    {
        return;
    }

    // Recursively build command-line representation based on node type
    switch (node->type)
    {
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        // Handle assignments first
        if (node->data.simple_command.assignments != NULL &&
            node->data.simple_command.assignments->size > 0)
        {
            for (int i = 0; i < node->data.simple_command.assignments->size; i++)
            {
                if (i > 0) string_append_cstr(result, " ");
                const token_t *assign = token_list_get(node->data.simple_command.assignments, i);
                miga_string_t *assign_str = token_to_cmd_string(assign);
                string_append(result, assign_str);
                string_destroy(&assign_str);
            }
            if (node->data.simple_command.words != NULL &&
                node->data.simple_command.words->size > 0)
            {
                string_append_cstr(result, " ");
            }
        }

        // Handle words (command and arguments)
        if (node->data.simple_command.words != NULL &&
            node->data.simple_command.words->size > 0)
        {
            for (int i = 0; i < node->data.simple_command.words->size; i++)
            {
                if (i > 0) string_append_cstr(result, " ");
                const token_t *word = token_list_get(node->data.simple_command.words, i);
                miga_string_t *word_str = token_to_cmd_string(word);
                string_append(result, word_str);
                string_destroy(&word_str);
            }
        }

        // Handle redirections
        if (node->data.simple_command.redirections != NULL &&
            miga_node_list_size(node->data.simple_command.redirections) > 0)
        {
            for (int i = 0; i < miga_node_list_size(node->data.simple_command.redirections); i++)
            {
                string_append_cstr(result, " ");
                miga_node_t *redir = miga_node_list_get(node->data.simple_command.redirections, i);
                ast_node_to_command_line_full_helper(redir, result, level + 1);
            }
        }
        break;

    case MIGA_NODE_TYPE_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            if (node->data.pipeline.is_negated)
            {
                string_append_cstr(result, "! ");
            }
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (i > 0) string_append_cstr(result, " | ");
                ast_node_to_command_line_full_helper(node->data.pipeline.commands->nodes[i], result, level + 1);
            }
        }
        break;

    case MIGA_NODE_TYPE_AND_OR_LIST:
        ast_node_to_command_line_full_helper(node->data.andor_list.left, result, level + 1);
        string_append_cstr(result, node->data.andor_list.op == MIGA_ANDOR_OP_AND ? " && " : " || ");
        ast_node_to_command_line_full_helper(node->data.andor_list.right, result, level + 1);
        break;

    case MIGA_NODE_TYPE_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (i > 0)
                {
                    if (node->data.command_list.separators != NULL &&
                        i - 1 < node->data.command_list.separators->len)
                    {
                        miga_cmd_exec_t sep = miga_cmd_exec_list_get(node->data.command_list.separators, i - 1);
                        switch (sep)
                        {
                        case MIGA_CMD_EXEC_SEQUENTIAL:
                            string_append_cstr(result, " ; ");
                            break;
                        case MIGA_CMD_EXEC_BACKGROUND:
                            string_append_cstr(result, " & ");
                            break;
                        case MIGA_CMD_EXEC_END:
                            string_append_cstr(result, "\n");
                            break;
                        }
                    }
                    else
                    {
                        string_append_cstr(result, " ; ");
                    }
                }
                ast_node_to_command_line_full_helper(node->data.command_list.items->nodes[i], result, level + 1);
            }
        }
        break;

    case MIGA_NODE_TYPE_SUBSHELL:
        string_append_cstr(result, "( ");
        ast_node_to_command_line_full_helper(node->data.compound.body, result, level + 1);
        string_append_cstr(result, " )");
        break;

    case MIGA_NODE_TYPE_BRACE_GROUP:
        string_append_cstr(result, "{ ");
        ast_node_to_command_line_full_helper(node->data.compound.body, result, level + 1);
        string_append_cstr(result, " }");
        break;

    case MIGA_NODE_TYPE_IF_CLAUSE:
        string_append_cstr(result, "if ");
        ast_node_to_command_line_full_helper(node->data.if_clause.condition, result, level + 1);
        string_append_cstr(result, " ; then ");
        ast_node_to_command_line_full_helper(node->data.if_clause.then_body, result, level + 1);

        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < miga_node_list_size(node->data.if_clause.elif_list); i++)
            {
                miga_node_t *elif_node = miga_node_list_get(node->data.if_clause.elif_list, i);
                string_append_cstr(result, " ; elif ");
                ast_node_to_command_line_full_helper(elif_node->data.if_clause.condition, result, level + 1);
                string_append_cstr(result, " ; then ");
                ast_node_to_command_line_full_helper(elif_node->data.if_clause.then_body, result, level + 1);
            }
        }

        if (node->data.if_clause.else_body != NULL)
        {
            string_append_cstr(result, " ; else ");
            ast_node_to_command_line_full_helper(node->data.if_clause.else_body, result, level + 1);
        }

        string_append_cstr(result, " ; fi");
        break;

    case MIGA_NODE_TYPE_WHILE_CLAUSE:
        string_append_cstr(result, "while ");
        ast_node_to_command_line_full_helper(node->data.loop_clause.condition, result, level + 1);
        string_append_cstr(result, " ; do ");
        ast_node_to_command_line_full_helper(node->data.loop_clause.body, result, level + 1);
        string_append_cstr(result, " ; done");
        break;

    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        string_append_cstr(result, "until ");
        ast_node_to_command_line_full_helper(node->data.loop_clause.condition, result, level + 1);
        string_append_cstr(result, " ; do ");
        ast_node_to_command_line_full_helper(node->data.loop_clause.body, result, level + 1);
        string_append_cstr(result, " ; done");
        break;

    case MIGA_NODE_TYPE_FOR_CLAUSE:
        string_append_cstr(result, "for ");
        if (node->data.for_clause.variable != NULL)
        {
            string_append(result, node->data.for_clause.variable);
        }
        string_append_cstr(result, " in ");

        if (node->data.for_clause.words != NULL && node->data.for_clause.words->size > 0)
        {
            for (int i = 0; i < node->data.for_clause.words->size; i++)
            {
                if (i > 0) string_append_cstr(result, " ");
                const token_t *word = token_list_get(node->data.for_clause.words, i);
                miga_string_t *word_str = token_to_cmd_string(word);
                string_append(result, word_str);
                string_destroy(&word_str);
            }
        }

        string_append_cstr(result, " ; do ");
        ast_node_to_command_line_full_helper(node->data.for_clause.body, result, level + 1);
        string_append_cstr(result, " ; done");
        break;

    case MIGA_NODE_TYPE_CASE_CLAUSE:
        string_append_cstr(result, "case ");
        if (node->data.case_clause.word != NULL)
        {
            miga_string_t *word_str = token_to_cmd_string(node->data.case_clause.word);
            string_append(result, word_str);
            string_destroy(&word_str);
        }
        string_append_cstr(result, " in ");

        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < miga_node_list_size(node->data.case_clause.case_items); i++)
            {
                if (i > 0) string_append_cstr(result, " ");
                miga_node_t *case_item = miga_node_list_get(node->data.case_clause.case_items, i);
                ast_node_to_command_line_full_helper(case_item, result, level + 1);
            }
        }

        string_append_cstr(result, " esac");
        break;

    case MIGA_NODE_TYPE_CASE_ITEM:
        if (node->data.case_item.patterns != NULL && node->data.case_item.patterns->size > 0)
        {
            for (int i = 0; i < node->data.case_item.patterns->size; i++)
            {
                if (i > 0) string_append_cstr(result, " | ");
                const token_t *pattern = token_list_get(node->data.case_item.patterns, i);
                miga_string_t *pattern_str = token_to_cmd_string(pattern);
                string_append(result, pattern_str);
                string_destroy(&pattern_str);
            }
        }
        string_append_cstr(result, ") ");
        ast_node_to_command_line_full_helper(node->data.case_item.body, result, level + 1);
        string_append_cstr(result, " ;;");
        break;

    case MIGA_NODE_TYPE_FUNCTION_DEF:
        if (node->data.function_def.name != NULL)
        {
            string_append(result, node->data.function_def.name);
        }
        string_append_cstr(result, "() { ");
        ast_node_to_command_line_full_helper(node->data.function_def.body, result, level + 1);
        string_append_cstr(result, " }");
        break;

    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        ast_node_to_command_line_full_helper(node->data.redirected_command.command, result, level + 1);
        if (node->data.redirected_command.redirections != NULL &&
            miga_node_list_size(node->data.redirected_command.redirections) > 0)
        {
            for (int i = 0; i < miga_node_list_size(node->data.redirected_command.redirections); i++)
            {
                string_append_cstr(result, " ");
                miga_node_t *redir = miga_node_list_get(node->data.redirected_command.redirections, i);
                ast_node_to_command_line_full_helper(redir, result, level + 1);
            }
        }
        break;

    case MIGA_NODE_TYPE_REDIRECTION:
        if (node->data.redirection.io_number >= 0)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", node->data.redirection.io_number);
            string_append_cstr(result, buf);
        }

        string_append_cstr(result, miga_redir_to_cstr(node->data.redirection.redir_type));

        switch (node->data.redirection.operand)
        {
        case MIGA_REDIR_TARGET_FILE:
        case MIGA_REDIR_TARGET_FD:
            if (node->data.redirection.target != NULL)
            {
                miga_string_t *target_str = token_to_cmd_string(node->data.redirection.target);
                string_append(result, target_str);
                string_destroy(&target_str);
            }
            break;
        case MIGA_REDIR_TARGET_FD_STRING:
            if (node->data.redirection.fd_string != NULL)
            {
                string_append(result, node->data.redirection.fd_string);
            }
            break;
        case MIGA_REDIR_TARGET_BUFFER:
            if (node->data.redirection.target != NULL)
            {
                miga_string_t *target_str = token_to_cmd_string(node->data.redirection.target);
                string_append(result, target_str);
                string_destroy(&target_str);
            }
            if (node->data.redirection.buffer != NULL)
            {
                string_append_cstr(result, "\n");
                string_append(result, node->data.redirection.buffer);
            }
            break;
        case MIGA_REDIR_TARGET_CLOSE:
        case MIGA_REDIR_TARGET_INVALID:
        default:
            break;
        }
        break;

    case MIGA_NODE_TYPE_FUNCTION_STORED:
        // No representation for stored functions
        break;

    case MIGA_NODE_TYPE_COUNT:
    default:
        // Unknown node type
        string_append_cstr(result, "<unknown>");
        break;
    }
}

miga_string_t* miga_node_to_command_line_full(const miga_node_t* node)
{
    miga_string_t *str = string_create();
    ast_node_to_command_line_full_helper(node, str, 0);
    return str;
}

miga_cmd_exec_list_t *miga_cmd_exec_list_create(void)
{
    miga_cmd_exec_list_t *list = (miga_cmd_exec_list_t *)miga_malloc(sizeof(miga_cmd_exec_list_t));
    list->separators = (miga_cmd_exec_t *)miga_malloc(INITIAL_LIST_CAPACITY * sizeof(miga_cmd_exec_t));
    list->len = 0;
    list->capacity = INITIAL_LIST_CAPACITY;
    return list;
}

miga_cmd_exec_list_t *miga_cmd_exec_list_clone(const miga_cmd_exec_list_t *other)
{
    MIGA_EXPECTS_NOT_NULL(other);

    miga_cmd_exec_list_t *new_list = miga_cmd_exec_list_create();
    if (!new_list)
        return NULL;

    for (int i = 0; i < other->len; i++)
    {
        miga_cmd_exec_list_add(new_list, other->separators[i]);
    }

    return new_list;
}

void miga_cmd_exec_list_destroy(miga_cmd_exec_list_t **lst)
{
    if (!lst || !*lst)
        return;
    miga_cmd_exec_list_t *l = *lst;
    xfree(l->separators);
    xfree(l);
    *lst = NULL;
}

void miga_cmd_exec_list_add(miga_cmd_exec_list_t *list, miga_cmd_exec_t sep)
{
    MIGA_EXPECTS_NOT_NULL(list);
    if (list->len >= list->capacity)
    {
        int new_capacity = list->capacity * 2;
        list->separators =
            (miga_cmd_exec_t *)xrealloc(list->separators, new_capacity * sizeof(miga_cmd_exec_t));
        list->capacity = new_capacity;
    }
    list->separators[list->len++] = sep;
}

miga_cmd_exec_t miga_cmd_exec_list_get(const miga_cmd_exec_list_t *list, int index)
{
    MIGA_EXPECTS_NOT_NULL(list);
    MIGA_EXPECTS(index >= 0);
    Expects_lt(index, list->len);
    return list->separators[index];
}


/* ============================================================================
 * AST Visitor Pattern Support
 * ============================================================================ */

typedef bool (*ast_visitor_fn)(const miga_node_t *node, void *user_data);

static bool ast_traverse_helper(const miga_node_t *node, ast_visitor_fn visitor, void *user_data)
{
    if (node == NULL)
        return true;

    if (!visitor(node, user_data))
        return false;

    switch (node->type)
    {
    case MIGA_NODE_TYPE_SIMPLE_COMMAND:
        break;

    case MIGA_NODE_TYPE_PIPELINE:
        if (node->data.pipeline.commands != NULL)
        {
            for (int i = 0; i < node->data.pipeline.commands->size; i++)
            {
                if (!ast_traverse_helper(node->data.pipeline.commands->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case MIGA_NODE_TYPE_AND_OR_LIST:
        if (!ast_traverse_helper(node->data.andor_list.left, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.andor_list.right, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_COMMAND_LIST:
        if (node->data.command_list.items != NULL)
        {
            for (int i = 0; i < node->data.command_list.items->size; i++)
            {
                if (!ast_traverse_helper(node->data.command_list.items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case MIGA_NODE_TYPE_SUBSHELL:
    case MIGA_NODE_TYPE_BRACE_GROUP:
        if (!ast_traverse_helper(node->data.compound.body, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_IF_CLAUSE:
        if (!ast_traverse_helper(node->data.if_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.if_clause.then_body, visitor, user_data))
            return false;
        if (node->data.if_clause.elif_list != NULL)
        {
            for (int i = 0; i < node->data.if_clause.elif_list->size; i++)
            {
                if (!ast_traverse_helper(node->data.if_clause.elif_list->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        if (!ast_traverse_helper(node->data.if_clause.else_body, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_WHILE_CLAUSE:
    case MIGA_NODE_TYPE_UNTIL_CLAUSE:
        if (!ast_traverse_helper(node->data.loop_clause.condition, visitor, user_data))
            return false;
        if (!ast_traverse_helper(node->data.loop_clause.body, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_FOR_CLAUSE:
        if (!ast_traverse_helper(node->data.for_clause.body, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_CASE_CLAUSE:
        if (node->data.case_clause.case_items != NULL)
        {
            for (int i = 0; i < node->data.case_clause.case_items->size; i++)
            {
                if (!ast_traverse_helper(node->data.case_clause.case_items->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case MIGA_NODE_TYPE_CASE_ITEM:
        if (!ast_traverse_helper(node->data.case_item.body, visitor, user_data))
            return false;
        break;

    case MIGA_NODE_TYPE_FUNCTION_DEF:
        if (!ast_traverse_helper(node->data.function_def.body, visitor, user_data))
            return false;
        if (node->data.function_def.redirections != NULL)
        {
            for (int i = 0; i < node->data.function_def.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.function_def.redirections->nodes[i], visitor,
                                         user_data))
                    return false;
            }
        }
        break;

    case MIGA_NODE_TYPE_REDIRECTED_COMMAND:
        if (!ast_traverse_helper(node->data.redirected_command.command, visitor, user_data))
            return false;
        if (node->data.redirected_command.redirections != NULL)
        {
            for (int i = 0; i < node->data.redirected_command.redirections->size; i++)
            {
                if (!ast_traverse_helper(node->data.redirected_command.redirections->nodes[i],
                                         visitor, user_data))
                    return false;
            }
        }
        break;

    default:
        break;
    }

    return true;
}

bool ast_traverse(const miga_node_t *root, ast_visitor_fn visitor, void *user_data)
{
    MIGA_EXPECTS_NOT_NULL(visitor);
    return ast_traverse_helper(root, visitor, user_data);
}
