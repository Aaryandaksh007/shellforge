#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"

int lex(const char *input, Token *tokens, int max_tokens)
{
    if (input == NULL || tokens == NULL || max_tokens <= 0) {
        return 0;
    }

    int count = 0;

    char *copy = strdup(input);

    if (copy == NULL) {
        perror("strdup");
        return 0;
    }

    char *token = strtok(copy, " \t\n");

    while (token != NULL && count < max_tokens) {

        strncpy(tokens[count].value,
                token,
                sizeof(tokens[count].value) - 1);

        tokens[count].value[sizeof(tokens[count].value) - 1] = '\0';

        count++;

        token = strtok(NULL, " \t\n");
    }

    free(copy);

    return count;
}
