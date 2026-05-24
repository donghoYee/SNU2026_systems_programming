#include "dynarray.h"
#include "token.h"
#include "util.h"
#include "lexsyn.h"
#include "snush.h"
#include "execute.h"
#include "job.h"

extern struct job_manager *manager;
extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;

/*--------------------------------------------------------------------*/
void block_signal(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) < 0) {
    	fprintf(stderr, 
			"[Error] block_signal: sigprocmask(%s, sig=%d) failed: %s\n",
            block ? "SIG_BLOCK" : "SIG_UNBLOCK", sig, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
void handle_sigchld(void) {
	int i;
	pid_t pid;
	int status;

	if (!sigchld_flag) return;

	block_signal(SIGCHLD, TRUE);
	block_signal(SIGINT, TRUE);
	sigchld_flag = 0;

	/* Only reap background jobs here; foreground jobs are
	 * reaped by wait_fg() to avoid double-reaping races. */
	for (i = 0; i < MAX_JOBS; i++) {
		struct job *job = &manager->jobs[i];
		if (!job->in_use || job->state != BACKGROUND) continue;

		while ((pid = waitpid(-job->pgid, &status, WNOHANG)) > 0) {
			remove_pid_from_job(job, pid);
		}

		if (pid < 0 && errno != ECHILD && errno != EINTR) {
			/* Unexpected error; continue anyway */
		}

		if (job->remaining_processes == 0) {
			enqueue_done(job->job_id, job->pgid);
			delete_job(job->job_id);
		}
	}

	block_signal(SIGCHLD, FALSE);
	block_signal(SIGINT, FALSE);
}
/*--------------------------------------------------------------------*/
void handle_sigint(void) {
	int i;

	if (!sigint_flag) return;

	block_signal(SIGINT, TRUE);
	block_signal(SIGCHLD, TRUE);
	sigint_flag = 0;

	for (i = 0; i < MAX_JOBS; i++) {
		struct job *job = &manager->jobs[i];
		if (job->in_use && job->state == FOREGROUND) {
			kill(-job->pgid, SIGINT);
		}
	}

	block_signal(SIGINT, FALSE);
	block_signal(SIGCHLD, FALSE);
}
/*--------------------------------------------------------------------*/
void dup2_e(int oldfd, int newfd, const char *func, const int line) {
	int ret;

	ret = dup2(oldfd, newfd);
	if (ret < 0) {
		fprintf(stderr, 
			"Error dup2(%d, %d): %s(%s) at (%s:%d)\n", 
			oldfd, newfd, strerror(errno), errno_name(errno), func, line);
		_exit(127);
	}
}
/*--------------------------------------------------------------------*/
/* Do not modify this function. It is used to check the signals and 
 * handle them accordingly. It is called in the main loop of snush.c.
 */
void check_signals(void) {
    handle_sigchld();
    handle_sigint();
}
/*--------------------------------------------------------------------*/
void redout_handler(char *fname) {
	int fd;

	fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error_print(NULL, PERROR);
		_exit(127);
	}

	dup2_e(fd, STDOUT_FILENO, __func__, __LINE__);
	close(fd);
}
/*--------------------------------------------------------------------*/
void redin_handler(char *fname) {
	int fd;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		error_print(NULL, PERROR);
		_exit(127);
	}

	dup2_e(fd, STDIN_FILENO, __func__, __LINE__);
	close(fd);
}
/*--------------------------------------------------------------------*/
void build_command_partial(DynArray_T oTokens, int start, 
						int end, char *args[]) {
	int i, redin = FALSE, redout = FALSE, cnt = 0;
	struct Token *t;

	/* Build command */
	for (i = start; i < end; i++) {
		t = dynarray_get(oTokens, i);

		if (t->token_type == TOKEN_WORD) {
			if (redin == TRUE) {
				redin_handler(t->token_value);
				redin = FALSE;
			}
			else if (redout == TRUE) {
				redout_handler(t->token_value);
				redout = FALSE;
			}
			else {
				args[cnt++] = t->token_value;
			}
		}
		else if (t->token_type == TOKEN_REDIN)
			redin = TRUE;
		else if (t->token_type == TOKEN_REDOUT)
			redout = TRUE;
	}

	if (cnt >= MAX_ARGS_CNT) 
		fprintf(stderr, "[BUG] args overflow! cnt=%d\n", cnt);

	args[cnt] = NULL;

#ifdef DEBUG
	for (i = 0; i < cnt; i++) {
		if (args[i] == NULL)
			printf("CMD: NULL\n");
		else
			printf("CMD: %s\n", args[i]);
	}
	printf("END\n");
#endif
}
/*--------------------------------------------------------------------*/
void build_command(DynArray_T oTokens, char *args[]) {
	build_command_partial(oTokens, 0, 
						dynarray_get_length(oTokens), 
						args);
}
/*--------------------------------------------------------------------*/
int execute_builtin_partial(DynArray_T toks, int start, int end,
                            enum BuiltinType btype, int in_child) {
    
	int argc = end - start;
	struct Token *t1;
	int ret;
    char *dir;

    switch (btype) {
    case B_EXIT:
        if (in_child) return 0;
        
		if (argc == 1) {
			dynarray_map(toks, free_token, NULL);
			dynarray_free(toks);
			exit(EXIT_SUCCESS);
		}
		else {
			error_print("exit does not take any parameters", FPRINTF);
			return -1;
		}

    case B_CD: {
        if (argc == 1) {
            dir = getenv("HOME");
            if (!dir) {
                error_print("cd: HOME variable not set", FPRINTF);
                return -1;
            }
        } 
		else if (argc == 2) {
            t1 = dynarray_get(toks, start + 1);
            if (t1 && t1->token_type == TOKEN_WORD) 
				dir = t1->token_value;
        } 
		else {
            error_print("cd: Too many parameters", FPRINTF);
            return -1;
        }

        ret = chdir(dir);
        if (ret < 0) {
            error_print(NULL, PERROR);
            return -1;
        }
        return 0;
    }

    default:
        error_print("Bug found in execute_builtin_partial", FPRINTF);
        return -1;
    }
}
/*--------------------------------------------------------------------*/
int execute_builtin(DynArray_T oTokens, enum BuiltinType btype) {
	return execute_builtin_partial(oTokens, 0, 
								dynarray_get_length(oTokens), btype, FALSE);
}
/*--------------------------------------------------------------------*/
/* 
 * You need to finish implementing job related APIs. (find_job_by_jid(),
 * remove_pid_from_job(), delete_job()) in job.c to handle the job.
 * Feel free to modify the format of the job API according to your design.
 */
void wait_fg(int jobid) {
	pid_t pid;
	int status;

	 // Find the job structure by job ID
    struct job *job = find_job_by_jid(jobid);
    if (!job) {
        fprintf(stderr, "Job: %d not found\n", jobid);
        return;
    }

    while (1) {
        pid = waitpid(-job->pgid, &status, 0);

        if (pid > 0) {
			// Remove the finished process from the job's pid list
			if (!remove_pid_from_job(job, pid)) {
				fprintf(stderr, "Pid %d not found in the job: %d list\n",
					pid, job->job_id);
			}

			if (job->remaining_processes == 0) break;
        }

        if (pid == 0) continue;

		if (pid < 0) {
			if (errno == EINTR) {
				/* SIGINT or SIGCHLD interrupted us. Forward SIGINT
				 * to the foreground process group so its members die. */
				if (sigint_flag) handle_sigint();
				continue;
			}
			if (errno == ECHILD) break;
			error_print("Unknown error waitpid() in wait_fg()", PERROR);
		}
    }

	// Clean up job table entry if all processes are done
    if (job->remaining_processes == 0)
        delete_job(job->job_id);
}
/*--------------------------------------------------------------------*/
void print_job(int jobid, pid_t pgid) {
    fprintf(stdout, 
		"[%d] Process group: %d running in the background\n", jobid, pgid);
}
/*--------------------------------------------------------------------*/
/* Restore default signal disposition in a freshly forked child. */
static void child_reset_signals(void) {
	signal(SIGINT,  SIG_DFL);
	signal(SIGCHLD, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);
	signal(SIGTTIN, SIG_DFL);

	block_signal(SIGCHLD, FALSE);
	block_signal(SIGINT,  FALSE);
}
/*--------------------------------------------------------------------*/
int fork_exec(DynArray_T oTokens, int is_background) {
	pid_t pid;
	int sync_pipe[2];
	int jobid;
	int total_tokens;
	pid_t pids_arr[1];

	total_tokens = dynarray_get_length(oTokens);
	if (is_background) total_tokens--;

	if (pipe(sync_pipe) < 0) {
		error_print(NULL, PERROR);
		return -1;
	}

	block_signal(SIGCHLD, TRUE);
	block_signal(SIGINT,  TRUE);

	pid = fork();
	if (pid < 0) {
		close(sync_pipe[0]);
		close(sync_pipe[1]);
		block_signal(SIGCHLD, FALSE);
		block_signal(SIGINT,  FALSE);
		error_print(NULL, PERROR);
		return -1;
	}

	if (pid == 0) {
		/* === Child === */
		char *args[MAX_ARGS_CNT];
		char buf;
		ssize_t n;

		/* New process group for clean signal isolation */
		setpgid(0, 0);
		child_reset_signals();

		/* Wait for parent to register the job */
		close(sync_pipe[1]);
		do {
			n = read(sync_pipe[0], &buf, 1);
		} while (n < 0 && errno == EINTR);
		close(sync_pipe[0]);

		/* Set up redirection and assemble argv */
		build_command_partial(oTokens, 0, total_tokens, args);

		if (args[0] == NULL) _exit(0);

		execvp(args[0], args);
		/* exec failed */
		error_print(args[0], PERROR);
		_exit(127);
	}

	/* === Parent === */
	/* Set the child's pgid here too (race with child's setpgid) */
	setpgid(pid, pid);

	pids_arr[0] = pid;
	jobid = add_job(pid, pids_arr, 1,
					is_background ? BACKGROUND : FOREGROUND);
	if (jobid < 0) {
		error_print("Failed to register job", FPRINTF);
		/* Let the child exec anyway; we leak but recover */
	}

	/* Release the child */
	close(sync_pipe[0]);
	close(sync_pipe[1]);

	block_signal(SIGCHLD, FALSE);
	block_signal(SIGINT,  FALSE);

	if (is_background) {
		print_job(jobid, pid);
	}
	else {
		wait_fg(jobid);
	}

	return jobid;
}
/*--------------------------------------------------------------------*/
int iter_pipe_fork_exec(int n_pipe, DynArray_T oTokens, int is_background) {
	int n_cmd = n_pipe + 1;
	int total_tokens;
	int seg_start[MAX_PROC_PER_JOB];
	int seg_end[MAX_PROC_PER_JOB];
	int seg_count = 0;
	int i, start;
	pid_t pids[MAX_PROC_PER_JOB];
	pid_t pgid = 0;
	int sync_pipe[2];
	int prev_pipe[2] = {-1, -1};
	int cur_pipe[2]  = {-1, -1};
	int jobid;

	total_tokens = dynarray_get_length(oTokens);
	if (is_background) total_tokens--;

	if (n_cmd > MAX_PROC_PER_JOB) {
		error_print("Too many pipeline stages", FPRINTF);
		return -1;
	}

	/* Split tokens into pipeline segments at TOKEN_PIPE boundaries */
	start = 0;
	for (i = 0; i < total_tokens; i++) {
		struct Token *t = dynarray_get(oTokens, i);
		if (t->token_type == TOKEN_PIPE) {
			seg_start[seg_count] = start;
			seg_end[seg_count]   = i;
			seg_count++;
			start = i + 1;
		}
	}
	seg_start[seg_count] = start;
	seg_end[seg_count]   = total_tokens;
	seg_count++;

	if (seg_count != n_cmd) {
		error_print("Pipeline split inconsistency", FPRINTF);
		return -1;
	}

	if (pipe(sync_pipe) < 0) {
		error_print(NULL, PERROR);
		return -1;
	}

	block_signal(SIGCHLD, TRUE);
	block_signal(SIGINT,  TRUE);

	for (i = 0; i < n_cmd; i++) {
		pid_t pid;

		if (i < n_cmd - 1) {
			if (pipe(cur_pipe) < 0) {
				close(sync_pipe[0]);
				close(sync_pipe[1]);
				if (i > 0) {
					close(prev_pipe[0]);
					close(prev_pipe[1]);
				}
				block_signal(SIGCHLD, FALSE);
				block_signal(SIGINT,  FALSE);
				error_print(NULL, PERROR);
				return -1;
			}
		}

		pid = fork();
		if (pid < 0) {
			close(sync_pipe[0]);
			close(sync_pipe[1]);
			if (i > 0) {
				close(prev_pipe[0]);
				close(prev_pipe[1]);
			}
			if (i < n_cmd - 1) {
				close(cur_pipe[0]);
				close(cur_pipe[1]);
			}
			block_signal(SIGCHLD, FALSE);
			block_signal(SIGINT,  FALSE);
			error_print(NULL, PERROR);
			return -1;
		}

		if (pid == 0) {
			/* === Child === */
			char *args[MAX_ARGS_CNT];
			char buf;
			ssize_t nr;
			enum BuiltinType btype;
			struct Token *first;

			/* All stages share the first child's pid as pgid */
			if (i == 0) setpgid(0, 0);
			else        setpgid(0, pgid);

			child_reset_signals();

			/* Set up pipe stdin */
			if (i > 0) {
				dup2_e(prev_pipe[0], STDIN_FILENO, __func__, __LINE__);
				close(prev_pipe[0]);
				close(prev_pipe[1]);
			}
			/* Set up pipe stdout */
			if (i < n_cmd - 1) {
				dup2_e(cur_pipe[1], STDOUT_FILENO, __func__, __LINE__);
				close(cur_pipe[0]);
				close(cur_pipe[1]);
			}

			/* Wait for parent to finish add_job */
			close(sync_pipe[1]);
			do {
				nr = read(sync_pipe[0], &buf, 1);
			} while (nr < 0 && errno == EINTR);
			close(sync_pipe[0]);

			/* Check whether this stage is a built-in */
			first = dynarray_get(oTokens, seg_start[i]);
			btype = check_builtin(first);

			if (btype != NORMAL) {
				/* Built-in inside a pipeline: execute directly
				 * in this child without execvp. */
				execute_builtin_partial(oTokens, seg_start[i],
										seg_end[i], btype, TRUE);
				_exit(0);
			}

			build_command_partial(oTokens, seg_start[i],
								  seg_end[i], args);

			if (args[0] == NULL) _exit(0);

			execvp(args[0], args);
			error_print(args[0], PERROR);
			_exit(127);
		}

		/* === Parent === */
		if (i == 0) pgid = pid;
		setpgid(pid, pgid);
		pids[i] = pid;

		/* Close prev pipe; we no longer need it in parent */
		if (i > 0) {
			close(prev_pipe[0]);
			close(prev_pipe[1]);
		}
		/* Promote current pipe to prev for next iteration */
		if (i < n_cmd - 1) {
			prev_pipe[0] = cur_pipe[0];
			prev_pipe[1] = cur_pipe[1];
		}
	}

	/* Register the entire pipeline as one job */
	jobid = add_job(pgid, pids, n_cmd,
					is_background ? BACKGROUND : FOREGROUND);

	/* Release all children (closing write end EOFs the readers) */
	close(sync_pipe[0]);
	close(sync_pipe[1]);

	block_signal(SIGCHLD, FALSE);
	block_signal(SIGINT,  FALSE);

	if (is_background) {
		print_job(jobid, pgid);
	}
	else {
		wait_fg(jobid);
	}

	return jobid;
}
/*--------------------------------------------------------------------*/