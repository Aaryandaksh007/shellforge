#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"

static char *duplicate_string(const char *src)
{
    size_t len = strlen(src) + 1;

    char *copy = malloc(len);

    if (copy == NULL)
        return NULL;

    memcpy(copy, src, len);

    return copy;
}

Command *parse_command(char *input)
{
    Command *cmd = calloc(1, sizeof(Command));

    if (cmd == NULL) {
        perror("calloc");
        return NULL;
    }

    char *p = input;

    while (*p != '\0' && cmd->argc < MAX_ARGS - 1) {

        /* Skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;

        if (*p == '\0')
            break;

        char *start = p;

        /* Find end of token */
        while (*p != '\0' &&
               *p != ' ' &&
               *p != '\t' &&
               *p != '\n') {
            p++;
        }

        size_t len = (size_t)(p - start);

        char *token = malloc(len + 1);

        if (token == NULL) {
            perror("malloc");
            free_command(cmd);
            return NULL;
        }

        memcpy(token, start, len);
        token[len] = '\0';

        cmd->argv[cmd->argc] = token;
        cmd->argc++;
    }

    cmd->argv[cmd->argc] = NULL;

    return cmd;
}

void free_command(Command *cmd)
{
    if (cmd == NULL)
        return;

    for (int i = 0; i < cmd->argc; i++)
        free(cmd->argv[i]);

    free(cmd);
}

Pipeline *parse_pipeline(char *input)
{
    Pipeline *pipeline = calloc(1, sizeof(Pipeline));

    if (pipeline == NULL) {
        perror("calloc");
        return NULL;
    }

    char *start = input;

    while (*start != '\0' &&
           pipeline->count < MAX_COMMANDS) {

        char *end = strchr(start, '|');

        if (end != NULL)
            *end = '\0';

        Command *cmd = parse_command(start);

        if (cmd == NULL) {
            free_pipeline(pipeline);
            return NULL;
        }

        if (cmd->argc > 0) {
            pipeline->commands[pipeline->count] = cmd;
            pipeline->count++;
        } else {
            free_command(cmd);
        }

        if (end == NULL)
            break;

        start = end + 1;
    }

    return pipeline;
}

void free_pipeline(Pipeline *pipeline)
{
    if (pipeline == NULL)
        return;

    for (int i = 0; i < pipeline->count; i++)
        free_command(pipeline->commands[i]);

    free(pipeline);
}
