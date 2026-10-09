#define _POSIX_C_SOURCE 200809L
#include "pagesim.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include "parser.h"
#include "executor.h"
#include "jobs.h"

#define INPUT_SIZE 1024


/*
 * Handle Ctrl+Z while ShellForge is sitting at its prompt.
 *
 * The shell itself must not be stopped.
 * We simply move to a new line and let the main loop
 * display the prompt again.
 */
static void handle_shell_sigstp(int sig)
{
    (void)sig;

    write(STDOUT_FILENO, "\n", 1);
}


/*
 * Initialize interactive job control for ShellForge.
 */
static void init_shell_job_control(void)
{
    pid_t shell_pgid;

    /*
     * Ignore signals that should never stop the shell itself.
     */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    /*
     * Handle Ctrl+Z ourselves.
     */
    signal(SIGTSTP, handle_shell_sigstp);

    /*
     * Ignore terminal background access signals.
     */
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    shell_pgid = getpid();

    /*
     * Put ShellForge into its own process group.
     */
    if (setpgid(shell_pgid, shell_pgid) == -1) {

        if (errno != EACCES)
            perror("shellforge: setpgid");
    }

    /*
     * Give the terminal to ShellForge.
     */
    if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1)
        perror("shellforge: tcsetpgrp");
}


int main(void)
{
    /*
     * Initialize the job table.
     */
    init_jobs();

    /*
     * Initialize interactive terminal/job control.
     */
    init_shell_job_control();

    char input[INPUT_SIZE];

    printf("=================================\n");
    printf("        ShellForge v0.5\n");
    printf("        Pipeline Engine\n");
    printf("=================================\n");

    while (1) {

        /*
         * Reap completed background processes.
         *
         * This prevents zombie processes.
         */
        reap_background_jobs();

        /*
         * Display shell prompt.
         */
        printf("shellforge> ");
        fflush(stdout);

        /*
         * Read a command.
         */
        if (fgets(input, sizeof(input), stdin) == NULL) {

            /*
             * Ctrl+Z / signal interrupted fgets().
             *
             * Clear the stream error and return
             * to the shell prompt.
             */
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }

            /*
             * EOF / Ctrl+D.
             */
            printf("\n");
            break;
        }

        /*
         * Ignore empty input.
         */
        if (input[0] == '\n')
            continue;

        /*
         * Parse the input into a pipeline.
         */
        Pipeline *pipeline = parse_pipeline(input);

        if (pipeline == NULL)
            continue;

        /*
         * Nothing to execute.
         */
        if (pipeline->count == 0) {
            free_pipeline(pipeline);
            continue;
        }

        /*
         * Execute the command/pipeline.
         */
        execute_pipeline(pipeline);

        /*
         * Free parser memory.
         */
        free_pipeline(pipeline);
    }

    return 0;
}
