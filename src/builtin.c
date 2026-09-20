#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "builtin.h"

int handle_builtin(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0) {
        return 0;
    }

    /* pwd */
    if (strcmp(cmd->argv[0], "pwd") == 0) {

        char cwd[PATH_MAX];

        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("pwd");
        } else {
            printf("my own pwd");
	printf("%s\n", cwd);
        }

        return 1;
    }

    /* cd */
    if (strcmp(cmd->argv[0], "cd") == 0) {

        const char *path;
        char expanded_path[PATH_MAX];

        /* cd -> HOME */
        if (cmd->argc < 2) {

            path = getenv("HOME");

            if (path == NULL) {
                fprintf(stderr, "cd: HOME not set\n");
                return 1;
            }

        } else {

            path = cmd->argv[1];

            /* Expand ~ */
            if (path[0] == '~') {

                const char *home = getenv("HOME");

                if (home == NULL) {
                    fprintf(stderr, "cd: HOME not set\n");
                    return 1;
                }

                if (path[1] == '\0') {

                    path = home;

                } else if (path[1] == '/') {

                    snprintf(
                        expanded_path,
                        sizeof(expanded_path),
                        "%s%s",
                        home,
                        path + 1
                    );

                    path = expanded_path;
                }
            }
        }

        if (chdir(path) != 0) {
            perror("cd");
        }

        return 1;
    }

    /* exit */
    if (strcmp(cmd->argv[0], "exit") == 0) {
        exit(0);
    }

    return 0;
}
