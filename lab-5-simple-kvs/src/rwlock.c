/*--------------------------------------------------------------------*/
/* rwlock.c                                                           */
/*--------------------------------------------------------------------*/
#include "rwlock.h"
/*--------------------------------------------------------------------*/
/* free to add header files and global variables */

/*--------------------------------------------------------------------*/
/*
 * Custom RW lock = FIFO RW lock + "quick read" (reader-priority) read.
 *
 * Default (non-quick) accesses are served in strict FIFO order using a
 * ticket scheme:
 *   - next_ticket : the next ticket number to hand out.
 *   - now_serving : the ticket whose turn it is to be evaluated.
 * A non-quick thread takes a ticket and waits until now_serving reaches
 * it. A reader, once it is its turn, admits itself and immediately
 * advances now_serving so that the *next* queued access can be
 * evaluated too -- this lets a run of consecutive readers proceed
 * concurrently. A writer keeps now_serving on its own ticket until it
 * releases the lock, so everyone behind it stays blocked (sequential
 * writers, no writer starvation).
 *
 * A "quick read" does NOT take a ticket. It only waits for the active
 * writer (if any) to finish and then immediately acquires the read
 * lock, jumping ahead of every pending FIFO access. Concurrent quick
 * reads run together; their relative order does not matter.
 *
 * A single condition variable is used; every waiter re-checks its own
 * predicate after a broadcast. With at most NUM_THREADS waiters this is
 * simple and efficient enough.
 */
struct uctx
{
    pthread_cond_t cond;       // all waiters wait/are woken here
    unsigned long next_ticket; // next FIFO ticket to hand out
    unsigned long now_serving; // FIFO ticket currently allowed to run
};
/*--------------------------------------------------------------------*/
int rwlock_init(rwlock_t *rw, int delay)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    struct uctx *u;

    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }

    u = calloc(1, sizeof(struct uctx));
    if (!u)
    {
        DEBUG_PRINT("Failed to allocate rwlock user context");
        return -1;
    }

    if (pthread_mutex_init(&rw->lock, NULL) != 0)
    {
        DEBUG_PRINT("Failed to init mutex");
        free(u);
        return -1;
    }
    if (pthread_cond_init(&u->cond, NULL) != 0)
    {
        DEBUG_PRINT("Failed to init cond");
        pthread_mutex_destroy(&rw->lock);
        free(u);
        return -1;
    }

    u->next_ticket = 0;
    u->now_serving = 0;
    rw->current_readers = 0;
    rw->current_writers = 0;
    rw->delay = (delay < 0) ? 0 : (unsigned int)delay;
    rw->uctx = u;

    return 0;
/*--------------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/
int rwlock_read_lock(rwlock_t *rw, int quick)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    struct uctx *u;

    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    u = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);

    if (quick)
    {
        /* Quick read: preempt the FIFO queue. Wait only for an active
         * writer to finish, then grab the read lock immediately. */
        while (rw->current_writers > 0)
            pthread_cond_wait(&u->cond, &rw->lock);

        rw->current_readers++;
    }
    else
    {
        /* Take a FIFO ticket and wait for our turn. */
        unsigned long ticket = u->next_ticket++;

        while (u->now_serving != ticket)
            pthread_cond_wait(&u->cond, &rw->lock);

        /* It is our turn. By FIFO, no writer can be active here. Admit
         * ourselves and hand the turn to the next access so that a
         * following reader can join us concurrently. */
        rw->current_readers++;
        u->now_serving++;
    }

    /* Wake others: a following concurrent reader, or a quick reader. */
    pthread_cond_broadcast(&u->cond);

    pthread_mutex_unlock(&rw->lock);
    return 0;
/*--------------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/
int rwlock_read_unlock(rwlock_t *rw)
{
    TRACE_PRINT();
    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    sleep(rw->delay);
/*--------------------------------------------------------------------*/
    struct uctx *u = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);

    if (rw->current_readers > 0)
        rw->current_readers--;

    /* When the last reader leaves, a pending writer at its turn can go. */
    if (rw->current_readers == 0)
        pthread_cond_broadcast(&u->cond);

    pthread_mutex_unlock(&rw->lock);
    return 0;
/*--------------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/
int rwlock_write_lock(rwlock_t *rw)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    struct uctx *u;
    unsigned long ticket;

    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    u = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);

    /* Take a FIFO ticket; wait for our turn AND for the lock to drain. */
    ticket = u->next_ticket++;
    while (u->now_serving != ticket ||
           rw->current_readers > 0 ||
           rw->current_writers > 0)
        pthread_cond_wait(&u->cond, &rw->lock);

    rw->current_writers++;
    /* Keep now_serving on our ticket: everyone behind us stays blocked
     * until rwlock_write_unlock() advances it. */

    pthread_mutex_unlock(&rw->lock);
    return 0;
/*--------------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/
int rwlock_write_unlock(rwlock_t *rw)
{
    TRACE_PRINT();
    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    sleep(rw->delay);
/*--------------------------------------------------------------------*/
    struct uctx *u = (struct uctx *)rw->uctx;

    pthread_mutex_lock(&rw->lock);

    if (rw->current_writers > 0)
        rw->current_writers--;

    /* Release our turn to the next pending access (oldest first). */
    u->now_serving++;
    pthread_cond_broadcast(&u->cond);

    pthread_mutex_unlock(&rw->lock);
    return 0;
/*--------------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/
int rwlock_destroy(rwlock_t *rw)
{
    TRACE_PRINT();
/*--------------------------------------------------------------------*/
    struct uctx *u;

    if (!rw)
    {
        errno = EINVAL;
        return -1;
    }
    u = (struct uctx *)rw->uctx;

    if (u)
    {
        pthread_cond_destroy(&u->cond);
        free(u);
        rw->uctx = NULL;
    }
    pthread_mutex_destroy(&rw->lock);

    return 0;
/*--------------------------------------------------------------------*/
}
