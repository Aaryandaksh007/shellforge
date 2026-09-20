#include <stdio.h>
#include <stdlib.h>

#include "parser.h"
#include "executor.h"

#define INPUT_SIZE 1024

int main(void)
{
    char input[INPUT_SIZE];

    printf("=================================\n");
    printf("        ShellForge v0.5\n");
    printf("        Pipeline Engine\n");
    printf("=================================\n");

    while (1) {

        printf("shellforge> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("\n");
            break;
        }

        if (input[0] == '\n')
            continue;

        Pipeline *pipeline = parse_pipeline(input);

        if (pipeline == NULL)
            continue;

        if (pipeline->count == 0) {
            free_pipeline(pipeline);
            continue;
        }

        execute_pipeline(pipeline);

        free_pipeline(pipeline);
    }

    return 0;
}
