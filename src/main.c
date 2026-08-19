#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "expand.h"

#define INPUT_SIZE 1024

int main(void)
{
    char input[INPUT_SIZE];

    printf("=================================\n");
    printf("        ShellForge v0.2\n");
    printf("     Parser + Expansion Engine\n");
    printf("=================================\n");

    while (1) {

        printf("shellforge> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        if (strcmp(input, "\n") == 0)
            continue;

        if (strncmp(input, "exit", 4) == 0 &&
            (input[4] == '\n' || input[4] == '\0')) {
            break;
        }

        Command *cmd = parse_command(input);

        if (cmd == NULL)
            continue;

        expand_command(cmd);

        printf("\nParsed command:\n");

        for (int i = 0; i < cmd->argc; i++) {
            printf("  argv[%d] = \"%s\"\n",
                   i,
                   cmd->argv[i]);
        }

        printf("\n");

        free_command(cmd);
    }

    printf("ShellForge terminated.\n");

    return 0;
}
