#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include "jobs.h"

static Job jobs[MAX_JOBS];
static int next_job_id = 1;

void init_jobs(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].id = 0;
        jobs[i].pgid = -1;
        jobs[i].command[0] = '\0';
        jobs[i].state = JOB_DONE;
    }

    next_job_id = 1;
}

int add_job(pid_t pgid, const char *command)
{
    for (int i = 0; i < MAX_JOBS; i++) {

        if (jobs[i].id == 0 ||
            jobs[i].state == JOB_DONE) {

            jobs[i].id = next_job_id++;
            jobs[i].pgid = pgid;

            strncpy(
                jobs[i].command,
                command,
                sizeof(jobs[i].command) - 1
            );

            jobs[i].command[
                sizeof(jobs[i].command) - 1
            ] = '\0';

            jobs[i].state = JOB_RUNNING;

            return jobs[i].id;
        }
    }

    return -1;
}

Job *get_job(int job_id)
{
    for (int i = 0; i < MAX_JOBS; i++) {

        if (jobs[i].id == job_id &&
            jobs[i].state != JOB_DONE) {

            return &jobs[i];
        }
    }

    return NULL;
}

void print_jobs(void)
{
    for (int i = 0; i < MAX_JOBS; i++) {

        if (jobs[i].id == 0)
            continue;

        if (jobs[i].state == JOB_RUNNING) {

            printf(
                "[%d] Running %s\n",
                jobs[i].id,
                jobs[i].command
            );
        }
        else if (jobs[i].state == JOB_STOPPED) {

            printf(
                "[%d] Stopped %s\n",
                jobs[i].id,
                jobs[i].command
            );
        }
    }
}

void reap_background_jobs(void)
{
    int status;

    while (1) {

        pid_t pid = waitpid(
            -1,
            &status,
            WNOHANG
        );

        /*
         * A child process was successfully collected.
         */
        if (pid > 0) {

            /*
             * Find the corresponding job.
             */
            for (int i = 0; i < MAX_JOBS; i++) {

                if (jobs[i].id == 0)
                    continue;

                if (jobs[i].pgid == pid) {

                    if (WIFEXITED(status) ||
                        WIFSIGNALED(status)) {

                        printf(
                            "\n[%d] Done %s\n",
                            jobs[i].id,
                            jobs[i].command
                        );

                        jobs[i].state = JOB_DONE;
                    }

                    break;
                }
            }

            /*
             * The important part:
             *
             * waitpid() already collected the child,
             * so it cannot remain a zombie even if
             * there is no matching job entry.
             */
            continue;
        }

        /*
         * No more exited children currently available.
         */
        if (pid == 0)
            break;

        /*
         * waitpid() failed.
         */
        if (pid == -1) {

            /*
             * Interrupted by a signal.
             * Try again.
             */
            if (errno == EINTR)
                continue;

            /*
             * No child processes remain.
             */
            if (errno == ECHILD)
                break;

            perror("waitpid");
            break;
        }
    }
}
