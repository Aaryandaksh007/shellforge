#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>

#include "executor.h"
#include "jobs.h"
#include "pagesim.h"

/*
 * ============================================================
 * BUILTIN CHECK
 * ============================================================
 */

static int is_builtin(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0)
        return 0;

    return strcmp(cmd->argv[0], "cd") == 0 ||
           strcmp(cmd->argv[0], "pwd") == 0 ||
           strcmp(cmd->argv[0], "echo") == 0 ||
           strcmp(cmd->argv[0], "help") == 0 ||
           strcmp(cmd->argv[0], "jobs") == 0 ||
           strcmp(cmd->argv[0], "fg") == 0 ||
           strcmp(cmd->argv[0], "pagesim") == 0 ||
           strcmp(cmd->argv[0], "exit") == 0;
}

/*
 * ============================================================
 * SIGNAL HANDLING
 * ============================================================
 */

static void restore_child_signals(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
}

/*
 * ============================================================
 * TERMINAL CONTROL
 * ============================================================
 */

static void give_terminal_to_shell(void)
{
    pid_t shell_pgid = getpgrp();

    if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1)
        perror("shellforge: tcsetpgrp");
}

/*
 * ============================================================
 * FOREGROUND JOB
 * ============================================================
 */

static void run_foreground_job(int job_id)
{
    Job *job = get_job(job_id);

    if (job == NULL) {
        printf("fg: no such job: %d\n", job_id);
        return;
    }

    /*
     * Give terminal to the job.
     */
    if (tcsetpgrp(STDIN_FILENO, job->pgid) == -1) {
        perror("fg: tcsetpgrp");
        return;
    }

    /*
     * Resume stopped job.
     */
    if (job->state == JOB_STOPPED) {
        if (kill(-job->pgid, SIGCONT) == -1) {
            perror("fg: SIGCONT");
            give_terminal_to_shell();
            return;
        }

        job->state = JOB_RUNNING;
    }

    int status;

    /*
     * Wait for the process group.
     *
     * WUNTRACED detects Ctrl+Z.
     */
    while (1) {
        pid_t result = waitpid(
            -job->pgid,
            &status,
            WUNTRACED
        );

        if (result == -1) {
            if (errno == EINTR)
                continue;

            if (errno == ECHILD)
                break;

            perror("fg: waitpid");
            break;
        }

        /*
         * Job stopped.
         */
        if (WIFSTOPPED(status)) {
            job->state = JOB_STOPPED;

            printf(
                "\n[%d] Stopped %s\n",
                job->id,
                job->command
            );

            give_terminal_to_shell();
            return;
        }

        /*
         * Child terminated.
         */
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            pid_t remaining;

            do {
                remaining = waitpid(
                    -job->pgid,
                    &status,
                    WUNTRACED | WNOHANG
                );

                if (remaining > 0 &&
                    WIFSTOPPED(status)) {
                    job->state = JOB_STOPPED;

                    printf(
                        "\n[%d] Stopped %s\n",
                        job->id,
                        job->command
                    );

                    give_terminal_to_shell();
                    return;
                }
            } while (remaining > 0);

            job->state = JOB_DONE;
            break;
        }
    }

    give_terminal_to_shell();
}

/*
 * ============================================================
 * PAGESIM BUILTIN
 * ============================================================
 *
 * Supported examples:
 *
 * pagesim
 * pagesim --frames 3 --algo fifo --ref "1 2 3 1 4"
 * pagesim --frames 3 --algo fifo --trace traces/basic.txt
 * pagesim --algo working-set --window 3 --trace traces/basic.txt
 * pagesim --frames 3 --algo compare --trace traces/basic.txt
 * pagesim --frames 4 --algo anomaly --trace traces/basic.txt
 */

static void run_pagesim(Command *cmd)
{
    int frames = 3;
    int window_size = 3;

    const char *algo = "fifo";
    const char *trace_path = NULL;

    int ref_was_set = 0;

    char ref[4096] = "1 2 3 1 4 2 5 1 2 3 4";

    if (cmd == NULL)
        return;

    /*
     * Parse PageSim arguments.
     */
    for (int i = 1; i < cmd->argc; i++) {

        /*
         * Number of frames.
         */
        if (strcmp(cmd->argv[i], "--frames") == 0) {
            if (i + 1 >= cmd->argc) {
                printf("pagesim: missing value for --frames\n");
                return;
            }

            char *end = NULL;
            long value = strtol(cmd->argv[++i], &end, 10);

            if (end == cmd->argv[i] ||
                *end != '\0' ||
                value <= 0 ||
                value > 64) {
                printf(
                    "pagesim: frames must be between 1 and 64\n"
                );
                return;
            }

            frames = (int)value;
        }

        /*
         * Replacement algorithm or analysis mode.
         */
        else if (strcmp(cmd->argv[i], "--algo") == 0) {
            if (i + 1 >= cmd->argc) {
                printf("pagesim: missing value for --algo\n");
                return;
            }

            algo = cmd->argv[++i];
        }

        /*
         * Working-set window size.
         */
        else if (strcmp(cmd->argv[i], "--window") == 0) {
            if (i + 1 >= cmd->argc) {
                printf("pagesim: missing value for --window\n");
                return;
            }

            char *end = NULL;
            long value = strtol(cmd->argv[++i], &end, 10);

            if (end == cmd->argv[i] ||
                *end != '\0' ||
                value < 1 ||
                value > 256) {
                printf(
                    "pagesim: window must be between 1 and 256\n"
                );
                return;
            }

            window_size = (int)value;
        }

        /*
         * Read reference data from a trace file.
         */
        else if (strcmp(cmd->argv[i], "--trace") == 0) {
            if (i + 1 >= cmd->argc) {
                printf(
                    "pagesim: missing file path for --trace\n"
                );
                return;
            }

            if (trace_path != NULL) {
                printf(
                    "pagesim: --trace specified more than once\n"
                );
                return;
            }

            trace_path = cmd->argv[++i];
        }

        /*
         * Read references from command-line arguments.
         *
         * --ref consumes all remaining arguments.
         */
        else if (strcmp(cmd->argv[i], "--ref") == 0) {
            if (i + 1 >= cmd->argc) {
                printf("pagesim: missing value for --ref\n");
                return;
            }

            if (ref_was_set) {
                printf(
                    "pagesim: --ref specified more than once\n"
                );
                return;
            }

            ref_was_set = 1;
            ref[0] = '\0';

            for (i++; i < cmd->argc; i++) {
                size_t used = strlen(ref);
                size_t available = sizeof(ref) - used - 1;
                size_t length = strlen(cmd->argv[i]);
                size_t separator = used > 0 ? 1U : 0U;

                if (length + separator > available) {
                    printf(
                        "pagesim: reference string is too long\n"
                    );
                    return;
                }

                if (separator)
                    ref[used++] = ' ';

                memcpy(
                    ref + used,
                    cmd->argv[i],
                    length + 1
                );
            }

            break;
        }

        /*
         * Unknown option.
         */
        else {
            printf(
                "pagesim: unknown option: %s\n",
                cmd->argv[i]
            );

            printf(
                "Usage: pagesim [--frames N] [--algo ALGORITHM] "
                "[--window N] "
                "[--trace FILE | --ref \"PAGE PAGE PAGE ...\"]\n"
            );

            return;
        }
    }

    /*
     * Do not allow both reference input methods.
     */
    if (trace_path != NULL && ref_was_set) {
        printf(
            "pagesim: use either --trace or --ref, not both\n"
        );
        return;
    }

    /*
     * Load the trace file.
     */
    if (trace_path != NULL) {
        FILE *fp = fopen(trace_path, "r");

        if (fp == NULL) {
            perror("pagesim: cannot open trace file");
            return;
        }

        size_t used = 0;
        char line[512];

        ref[0] = '\0';

        while (fgets(line, sizeof(line), fp) != NULL) {
            size_t length = strlen(line);

            if (length > sizeof(ref) - used - 1) {
                printf("pagesim: trace file is too large\n");
                fclose(fp);
                return;
            }

            memcpy(ref + used, line, length + 1);
            used += length;
        }

        if (ferror(fp)) {
            perror("pagesim: error reading trace file");
            fclose(fp);
            return;
        }

        fclose(fp);

        if (used == 0) {
            printf("pagesim: trace file is empty\n");
            return;
        }
    }

    /*
     * Display the command configuration.
     */
    printf("\n");
    printf("====================================\n");
    printf("          ShellForge PageSim\n");
    printf("====================================\n");
    printf("Algorithm : %s\n", algo);
    printf("Frames    : %d\n", frames);

    if (trace_path != NULL)
        printf("Trace file: %s\n", trace_path);
    else
        printf("Reference : %s\n", ref);

    printf("------------------------------------\n");

    /*
     * IMPORTANT:
     * pagesim.h must declare pagesim_run with four arguments.
     */
    pagesim_run(
        frames,
        algo,
        ref,
        window_size
    );

    printf("------------------------------------\n");
}

/*
 * ============================================================
 * BUILTIN EXECUTION
 * ============================================================
 */

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
        printf("  cd       Change directory\n");
        printf("  pwd      Print working directory\n");
        printf("  echo     Print text\n");
        printf("  help     Show this help\n");
        printf("  jobs     Show background jobs\n");
        printf("  fg       Bring a job to foreground\n");
        printf("  pagesim  Run demand-paging simulator\n");
        printf("  exit     Exit ShellForge\n");

        printf("\nPageSim usage:\n");
        printf("  pagesim\n");
        printf(
            "  pagesim --frames 3 --algo fifo "
            "--ref \"1 2 3 1 4 2 5\"\n"
        );
        printf(
            "  pagesim --frames 3 --algo fifo "
            "--trace traces/basic.txt\n"
        );
        printf(
            "  pagesim --algo working-set --window 3 "
            "--trace traces/basic.txt\n"
        );
        printf(
            "  pagesim --frames 3 --algo compare "
            "--trace traces/basic.txt\n"
        );
        printf(
            "  pagesim --frames 4 --algo anomaly "
            "--trace traces/basic.txt\n"
        );

        printf("\nPageSim algorithms and modes:\n");
        printf("  fifo\n");
        printf("  lru\n");
        printf("  lfu\n");
        printf("  clock\n");
        printf("  hybrid\n");
        printf("  optimal\n");
        printf("  compare\n");
        printf("  anomaly\n");
        printf("  working-set\n");
    }

    else if (strcmp(cmd->argv[0], "jobs") == 0) {
        print_jobs();
    }

    else if (strcmp(cmd->argv[0], "fg") == 0) {
        if (cmd->argc < 2) {
            printf("fg: usage: fg <job>\n");
            return;
        }

        char *job_arg = cmd->argv[1];

        if (job_arg[0] == '%')
            job_arg++;

        char *endptr;
        long job_id = strtol(job_arg, &endptr, 10);

        if (*job_arg == '\0' || *endptr != '\0') {
            printf(
                "fg: invalid job: %s\n",
                cmd->argv[1]
            );
            return;
        }

        run_foreground_job((int)job_id);
    }

    else if (strcmp(cmd->argv[0], "pagesim") == 0) {
        run_pagesim(cmd);
    }
}

/*
 * ============================================================
 * SINGLE COMMAND EXECUTION
 * ============================================================
 */

void execute_command(Command *cmd)
{
    if (cmd == NULL || cmd->argc == 0)
        return;

    if (strcmp(cmd->argv[0], "exit") == 0)
        exit(0);

    /*
     * Builtins run directly inside the shell process.
     */
    if (is_builtin(cmd)) {
        run_builtin(cmd);
        return;
    }

    /*
     * External command.
     */
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    /*
     * CHILD
     */
    if (pid == 0) {
        restore_child_signals();

        /*
         * Child becomes leader of its own process group.
         */
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(127);
        }

        execvp(cmd->argv[0], cmd->argv);

        perror("shellforge");
        _exit(127);
    }

    /*
     * PARENT
     *
     * Put child into its own process group too.
     */
    if (setpgid(pid, pid) == -1) {
        if (errno != EACCES && errno != ESRCH)
            perror("setpgid");
    }

    /*
     * BACKGROUND JOB
     */
    if (cmd->background) {
        char command[256];
        command[0] = '\0';

        for (int i = 0; i < cmd->argc; i++) {
            if (i > 0) {
                strncat(
                    command,
                    " ",
                    sizeof(command) - strlen(command) - 1
                );
            }

            strncat(
                command,
                cmd->argv[i],
                sizeof(command) - strlen(command) - 1
            );
        }

        strncat(
            command,
            " &",
            sizeof(command) - strlen(command) - 1
        );

        int job_id = add_job(pid, command);

        if (job_id > 0)
            printf("[%d] %d\n", job_id, pid);
        else
            printf("[background] PID %d\n", pid);

        return;
    }

    /*
     * FOREGROUND JOB
     */
    if (tcsetpgrp(STDIN_FILENO, pid) == -1)
        perror("shellforge: tcsetpgrp");

    int status;

    while (1) {
        pid_t result = waitpid(
            pid,
            &status,
            WUNTRACED
        );

        if (result == -1) {
            if (errno == EINTR)
                continue;

            perror("waitpid");
            break;
        }

        /*
         * Ctrl+Z stopped the foreground process.
         */
        if (WIFSTOPPED(status)) {
            char command[256];
            command[0] = '\0';

            for (int i = 0; i < cmd->argc; i++) {
                if (i > 0) {
                    strncat(
                        command,
                        " ",
                        sizeof(command) - strlen(command) - 1
                    );
                }

                strncat(
                    command,
                    cmd->argv[i],
                    sizeof(command) - strlen(command) - 1
                );
            }

            int job_id = add_job(pid, command);

            if (job_id > 0) {
                Job *job = get_job(job_id);

                if (job != NULL)
                    job->state = JOB_STOPPED;

                printf(
                    "\n[%d] Stopped %s\n",
                    job_id,
                    command
                );
            }

            give_terminal_to_shell();
            return;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status))
            break;
    }

    give_terminal_to_shell();
}

/*
 * ============================================================
 * PIPELINE EXECUTION
 * ============================================================
 */

void execute_pipeline(Pipeline *pipeline)
{
    if (pipeline == NULL || pipeline->count == 0)
        return;

    /*
     * Single command.
     */
    if (pipeline->count == 1) {
        execute_command(pipeline->commands[0]);
        return;
    }

    int pipefds[2 * (MAX_COMMANDS - 1)];
    pid_t pids[MAX_COMMANDS];

    int pipe_count = pipeline->count - 1;

    /*
     * Create all pipes.
     */
    for (int i = 0; i < pipe_count; i++) {
        if (pipe(&pipefds[i * 2]) == -1) {
            perror("pipe");
            return;
        }
    }

    pid_t pgid = 0;

    /*
     * Create pipeline processes.
     */
    for (int i = 0; i < pipeline->count; i++) {
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");

            for (int j = 0; j < 2 * pipe_count; j++)
                close(pipefds[j]);

            for (int j = 0; j < i; j++)
                waitpid(pids[j], NULL, 0);

            return;
        }

        /*
         * CHILD
         */
        if (pids[i] == 0) {
            restore_child_signals();

            /*
             * First process becomes process-group leader.
             */
            if (i == 0) {
                if (setpgid(0, 0) == -1) {
                    perror("setpgid");
                    _exit(127);
                }
            }
            else {
                if (setpgid(0, pgid) == -1) {
                    perror("setpgid");
                    _exit(127);
                }
            }

            /*
             * Connect stdin to the previous pipe.
             */
            if (i > 0) {
                int read_fd = pipefds[(i - 1) * 2];

                if (dup2(read_fd, STDIN_FILENO) == -1) {
                    perror("dup2 stdin");
                    _exit(127);
                }
            }

            /*
             * Connect stdout to the next pipe.
             */
            if (i < pipeline->count - 1) {
                int write_fd = pipefds[i * 2 + 1];

                if (dup2(write_fd, STDOUT_FILENO) == -1) {
                    perror("dup2 stdout");
                    _exit(127);
                }
            }

            /*
             * Close all pipe descriptors.
             */
            for (int j = 0; j < 2 * pipe_count; j++)
                close(pipefds[j]);

            /*
             * Execute command.
             */
            execvp(
                pipeline->commands[i]->argv[0],
                pipeline->commands[i]->argv
            );

            perror("shellforge");
            _exit(127);
        }

        /*
         * PARENT: set process group.
         */
        if (i == 0) {
            pgid = pids[i];

            if (setpgid(pids[i], pgid) == -1) {
                if (errno != EACCES && errno != ESRCH)
                    perror("setpgid");
            }
        }
        else {
            if (setpgid(pids[i], pgid) == -1) {
                if (errno != EACCES && errno != ESRCH)
                    perror("setpgid");
            }
        }
    }

    /*
     * Close pipes in parent.
     */
    for (int i = 0; i < 2 * pipe_count; i++)
        close(pipefds[i]);

    /*
     * Foreground pipeline.
     */
    if (tcsetpgrp(STDIN_FILENO, pgid) == -1)
        perror("shellforge: tcsetpgrp");

    for (int i = 0; i < pipeline->count; i++)
        waitpid(pids[i], NULL, WUNTRACED);

    give_terminal_to_shell();
}
