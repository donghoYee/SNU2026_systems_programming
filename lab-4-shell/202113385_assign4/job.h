#ifndef _JOB_H_
#define _JOB_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#define MAX_JOBS 16
#define MAX_PROC_PER_JOB 16

typedef enum State {
    UNKNOWN = 0,
    FOREGROUND,
    BACKGROUND,
    STOPPED,
} job_state;

/*
 * Job = The user's command line input
 * ex) if the user's command line input is "ps -ef | grep job" then
 * One job, Two processes.
 */
struct job {
    int in_use;
    int job_id;
    pid_t pgid;
    int n_processes;
    int remaining_processes;
    pid_t pids[MAX_PROC_PER_JOB];
    job_state state;
};

/*
 * One global variable for a job manager.
 * When a job is created, register it with the job manager,
 * regardless of whether it is a foreground or background job.
 */
struct job_manager {
    int n_jobs;
    struct job *jobs;
    int n_done;
    int done_jids[MAX_JOBS];
    pid_t done_pgids[MAX_JOBS];
};

void init_job_manager(void);
int add_job(pid_t pgid, pid_t *pids, int n_processes, job_state state);
struct job *find_job_by_jid(int job_id);
struct job *find_job_by_pid(pid_t pid);
int remove_pid_from_job(struct job *job, pid_t pid);
int delete_job(int job_id);
void enqueue_done(int job_id, pid_t pgid);

#endif /* _JOB_H_ */
