#ifndef LEXER_H
#define LEXER_H

#include "token.h"

int lex(const char *input, Token *tokens, int max_tokens);

#endif
