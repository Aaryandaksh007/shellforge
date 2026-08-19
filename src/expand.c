#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expand.h"

#define MAX_EXPANDED 1024

static char *expand_variable(const char *input)
{
    if (input[0] != '$')
        return strdup(input);

    const char *variable = input + 1;

    const char *value = getenv(variable);

    if (value == NULL)
        return strdup("");

    return strdup(value);
}

void expand_command(Command *cmd)
{
    if (cmd == NULL)
        return;

    for (int i = 0; i < cmd->argc; i++) {

        if (cmd->argv[i][0] == '$') {

            char *expanded = expand_variable(cmd->argv[i]);

            if (expanded != NULL) {
                free(cmd->argv[i]);
                cmd->argv[i] = expanded;
            }
        }
    }
}
