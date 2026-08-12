#include <stdio.h>
#include "token.h"

void print_token(const Token *token)
{
    printf("type=%d, value=\"%s\"\n",
           token->type,
           token->value);
}
