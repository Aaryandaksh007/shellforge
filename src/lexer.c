#include <string.h>
#include "lexer.h"

int lex(const char *input, Token *tokens, int max_tokens)
{
    int count = 0;

    char *copy = strdup(input);

    if (copy == NULL)
    {
        return -1;
    }

    char *token = strtok(copy, " ");

    while (token != NULL && count < max_tokens)
    {
        tokens[count].type = TOKEN_WORD;
        tokens[count].value = token;

        count++;

        token = strtok(NULL, " ");
    }

    return count;
}
