#ifndef JOBS_H
#define JOBS_H

#include <sys/types.h>

#define MAX_JOBS 64

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} JobState;

typedef struct {
    int id;
    pid_t pgid;
    char command[256];
    JobState state;
} Job;

void init_jobs(void);

int add_job(pid_t pgid, const char *command);

Job *get_job(int job_id);

void print_jobs(void);

/* Reap completed background jobs */
void reap_background_jobs(void);

#endif
