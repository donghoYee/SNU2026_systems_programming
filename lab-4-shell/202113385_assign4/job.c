#include "job.h"

extern struct job_manager *manager;
/*--------------------------------------------------------------------*/
void init_job_manager(void) {
    manager = (struct job_manager *)calloc(1, sizeof(struct job_manager));
    if (manager == NULL) {
        fprintf(stderr, "[Error] job manager allocation failed\n");
        exit(EXIT_FAILURE);
    }

    manager->jobs = (struct job *)calloc(MAX_JOBS, sizeof(struct job));
    if (manager->jobs == NULL) {
        fprintf(stderr, "[Error] job array allocation failed\n");
        exit(EXIT_FAILURE);
    }
    manager->n_jobs = 0;
    manager->n_done = 0;
}
/*--------------------------------------------------------------------*/
/* Allocate a new JID equal to (max in-use JID + 1); 0 if no active jobs. */
static int allocate_jid(void) {
    int i;
    int max = -1;

    for (i = 0; i < MAX_JOBS; i++) {
        if (manager->jobs[i].in_use && manager->jobs[i].job_id > max)
            max = manager->jobs[i].job_id;
    }
    return max + 1;
}
/*--------------------------------------------------------------------*/
int add_job(pid_t pgid, pid_t *pids, int n_processes, job_state state) {
    int i, slot;
    struct job *job;

    if (manager->n_jobs >= MAX_JOBS) return -1;
    if (n_processes > MAX_PROC_PER_JOB) return -1;

    /* Find an empty slot */
    slot = -1;
    for (i = 0; i < MAX_JOBS; i++) {
        if (!manager->jobs[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    job = &manager->jobs[slot];
    job->job_id = allocate_jid();
    job->in_use = 1;
    job->pgid = pgid;
    job->n_processes = n_processes;
    job->remaining_processes = n_processes;
    job->state = state;
    for (i = 0; i < n_processes; i++)
        job->pids[i] = pids[i];

    manager->n_jobs++;
    return job->job_id;
}
/*--------------------------------------------------------------------*/
struct job *find_job_by_jid(int job_id) {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if (manager->jobs[i].in_use && manager->jobs[i].job_id == job_id)
            return &manager->jobs[i];
    }
    return NULL;
}
/*--------------------------------------------------------------------*/
struct job *find_job_by_pid(pid_t pid) {
    int i, j;
    for (i = 0; i < MAX_JOBS; i++) {
        if (!manager->jobs[i].in_use) continue;
        for (j = 0; j < manager->jobs[i].n_processes; j++) {
            if (manager->jobs[i].pids[j] == pid)
                return &manager->jobs[i];
        }
    }
    return NULL;
}
/*--------------------------------------------------------------------*/
int remove_pid_from_job(struct job *job, pid_t pid) {
    int j;
    if (!job) return 0;
    for (j = 0; j < job->n_processes; j++) {
        if (job->pids[j] == pid) {
            job->pids[j] = -1;
            job->remaining_processes--;
            return 1;
        }
    }
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int job_id) {
    struct job *job;

    job = find_job_by_jid(job_id);
    if (!job) return 0;

    memset(job, 0, sizeof(*job));
    manager->n_jobs--;
    return 1;
}
/*--------------------------------------------------------------------*/
void enqueue_done(int job_id, pid_t pgid) {
    if (manager->n_done >= MAX_JOBS) return;
    manager->done_jids[manager->n_done] = job_id;
    manager->done_pgids[manager->n_done] = pgid;
    manager->n_done++;
}
/*--------------------------------------------------------------------*/
