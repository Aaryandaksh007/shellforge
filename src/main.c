#include <stdio.h>
#include <string.h>

int main(void)
{
    char command[100];
    char *argv[10];

    printf("shellforge$ ");

    fgets(command, sizeof(command), stdin);

    command[strcspn(command, "\n")] = '\0';

    int argc = 0;

    char *token = strtok(command, " ");

    while (token != NULL && argc < 9)
    {
        argv[argc] = token;
        argc++;

        token = strtok(NULL, " ");
    }

    argv[argc] = NULL;

    printf("\nTokens:\n");

    for (int i = 0; argv[i] != NULL; i++)
    {
        printf("argv[%d] = \"%s\"\n", i, argv[i]);
    }

    return 0;
}
