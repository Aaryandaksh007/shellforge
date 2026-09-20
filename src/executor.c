#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "executor.h"

static int is_builtin(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0)
        return 0;

    return strcmp(cmd->argv[0], "cd") == 0 ||
           strcmp(cmd->argv[0], "pwd") == 0 ||
           strcmp(cmd->argv[0], "echo") == 0 ||
           strcmp(cmd->argv[0], "help") == 0 ||
           strcmp(cmd->argv[0], "exit") == 0;
}

static void run_builtin(Command *cmd)
{
    if (strcmp(cmd->argv[0], "pwd") == 0) {

        char cwd[1024];

        if (getcwd(cwd, sizeof(cwd)) != NULL)
            printf("%s\n", cwd);
        else
            perror("pwd");
    }

    else if (strcmp(cmd->argv[0], "echo") == 0) {

        for (int i = 1; i < cmd->argc; i++) {
            printf("%s", cmd->argv[i]);

            if (i < cmd->argc - 1)
                printf(" ");
        }

        printf("\n");
    }

    else if (strcmp(cmd->argv[0], "cd") == 0) {

        const char *path;

        if (cmd->argc < 2)
            path = getenv("HOME");
        else
            path = cmd->argv[1];

        if (path == NULL)
            path = ".";

        if (chdir(path) != 0)
            perror("cd");
    }

    else if (strcmp(cmd->argv[0], "help") == 0) {

        printf("ShellForge builtins:\n");
        printf("  cd      Change directory\n");
        printf("  pwd     Print working directory\n");
        printf("  echo    Print text\n");
        printf("  help    Show this help\n");
        printf("  exit    Exit ShellForge\n");
    }
}

void execute_command(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0)
        return;

    if (strcmp(cmd->argv[0], "exit") == 0)
        exit(0);

    if (is_builtin(cmd)) {
        run_builtin(cmd);
        return;
    }

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {

        execvp(cmd->argv[0], cmd->argv);

        perror("shellforge");
        _exit(127);
    }

    waitpid(pid, NULL, 0);
}

void execute_pipeline(Pipeline *pipeline)
{
    if (pipeline == NULL || pipeline->count == 0)
        return;

    /*
     * No pipe needed for a single command.
     */
    if (pipeline->count == 1) {
        execute_command(pipeline->commands[0]);
        return;
    }

    int pipefds[2 * (MAX_COMMANDS - 1)];
    pid_t pids[MAX_COMMANDS];

    int pipe_count = pipeline->count - 1;

    /*
     * Create all pipes first.
     */
    for (int i = 0; i < pipe_count; i++) {

        if (pipe(&pipefds[i * 2]) == -1) {

            perror("pipe");

            return;
        }
    }

    /*
     * Create one child for each command.
     */
    for (int i = 0; i < pipeline->count; i++) {

        pids[i] = fork();

        if (pids[i] < 0) {

            perror("fork");

            /*
             * Close all pipe descriptors.
             */
            for (int j = 0; j < 2 * pipe_count; j++)
                close(pipefds[j]);

            /*
             * Wait for children already created.
             */
            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);

            return;
        }

        if (pids[i] == 0) {

            /*
             * If this is not the first command,
             * connect previous pipe READ end to stdin.
             */
            if (i > 0) {

                int read_fd = pipefds[(i - 1) * 2];

                if (dup2(read_fd, STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    _exit(127);
                }
            }

            /*
             * If this is not the last command,
             * connect current pipe WRITE end to stdout.
             */
            if (i < pipeline->count - 1) {

                int write_fd = pipefds[i * 2 + 1];

                if (dup2(write_fd, STDOUT_FILENO) == -1) {
                    perror("dup2 stdout");
                    _exit(127);
                }
            }

            /*
             * Close every pipe descriptor.
             * After dup2(), stdin/stdout already point
             * to the required pipe ends.
             */
            for (int j = 0; j < 2 * pipe_count; j++)
                close(pipefds[j]);

            /*
             * Execute the command.
             */
            execvp(
                pipeline->commands[i]->argv[0],
                pipeline->commands[i]->argv
            );

            perror("shellforge");
            _exit(127);
        }
    }

    /*
     * Parent no longer needs any pipe descriptors.
     */
    for (int i = 0; i < 2 * pipe_count; i++)
        close(pipefds[i]);

    /*
     * Wait for every child.
     */
    for (int i = 0; i < pipeline->count; i++)
        waitpid(pids[i], NULL, 0);
}
