/*--------------------------------------------------------------------*/
/* server.c                                                           */
/*--------------------------------------------------------------------*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/time.h>
#include "common.h"
#include "skvslib.h"
/*--------------------------------------------------------------------*/
/* free to add header files and global variables */
#include <netinet/in.h>
#include <sys/socket.h>

/*--------------------------------------------------------------------*/
/* Robustly send all len bytes; returns 0 on success, -1 on error. */
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -1; /* ECONNRESET / EPIPE / etc. */
        }
        sent += (size_t)n;
    }
    return 0;
}
/*--------------------------------------------------------------------*/
struct thread_args
{
    int listenfd;
    int idx;
    struct skvs_ctx *ctx;

    /*----------------------------------------------------------------*/
    /* free to use */

    /*----------------------------------------------------------------*/
};
/*--------------------------------------------------------------------*/
volatile static sig_atomic_t g_shutdown = 0;
/*--------------------------------------------------------------------*/
void *handle_client(void *arg)
{
    TRACE_PRINT();
    struct thread_args *args = (struct thread_args *)arg;
    struct skvs_ctx *ctx = args->ctx;
    int idx = args->idx;
    int listenfd = args->listenfd;
    /*----------------------------------------------------------------*/
    /* free to add any variables */

    /*----------------------------------------------------------------*/

    free(args);
    fprintf(stdout, "%dth worker ready\n", idx);
    fflush(stdout);

    /*----------------------------------------------------------------*/
    char rbuf[BUF_SIZE + 1];
    char wbuf[BUF_SIZE + 1];
    struct timeval to;
    int connfd;

    /* recv timeout so a blocked worker periodically rechecks shutdown */
    to.tv_sec = TIMEOUT;
    to.tv_usec = 0;

    while (!g_shutdown)
    {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);

        connfd = accept(listenfd, (struct sockaddr *)&cli, &clilen);
        if (connfd < 0)
        {
            /* timeout (SO_RCVTIMEO) or interrupted: loop to recheck flag */
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            DEBUG_PRINT("accept failed");
            continue;
        }

        /* per-connection recv timeout for responsive shutdown */
        setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

        /* session loop: keep-alive until the client closes the socket */
        size_t off = 0;
        while (!g_shutdown)
        {
            ssize_t n = recv(connfd, rbuf + off, BUF_SIZE - off, 0);
            if (n < 0)
            {
                if (errno == EINTR ||
                    errno == EAGAIN || errno == EWOULDBLOCK)
                    continue; /* timeout/interrupt: recheck g_shutdown */
                break;        /* ECONNRESET / other fatal error */
            }
            if (n == 0)
                break; /* client closed (empty line or EOF -> EOF here) */

            off += (size_t)n;

            size_t wlen = 0;
            int r = skvs_serve(ctx, rbuf, off, wbuf, &wlen);
            if (r < 0)
                break; /* internal error */
            if (r == 0)
            {
                /* request not complete yet; keep accumulating. If the
                 * buffer is full without a newline, drop it (the
                 * reference client never sends > BUF_SIZE). */
                if (off >= BUF_SIZE)
                    off = 0;
                continue;
            }

            /* complete request served; reply (half-duplex => no
             * leftover bytes to keep) */
            if (send_all(connfd, wbuf, wlen) < 0)
                break;
            off = 0;
        }

        close(connfd);
    }
    /*----------------------------------------------------------------*/

    return NULL;
}
/*--------------------------------------------------------------------*/
/* Signal handler for SIGINT */
void handle_sigint(int sig)
{
    int saved_errno = errno; /* be async-signal-safe: don't clobber errno */
    (void)sig;
    g_shutdown = 1;
    errno = saved_errno;
}
/*--------------------------------------------------------------------*/
int main(int argc, char *argv[])
{
    size_t hash_size = DEFAULT_HASH_SIZE;
    char *ip = DEFAULT_ANY_IP;
    int port = DEFAULT_PORT, opt;
    int num_threads = NUM_THREADS;
    int delay = RWLOCK_DELAY;
    /*----------------------------------------------------------------*/
    /* free to declare any variables */

    /*----------------------------------------------------------------*/

    /* parse command line options */
    while ((opt = getopt(argc, argv, "p:t:s:d:h")) != -1)
    {
        switch (opt)
        {
        case 'p':
            port = atoi(optarg);
            if (port < 1025 || port > 65535)
            {
                fprintf(stderr, "Invalid port number: %d\n", port);
                exit(EXIT_FAILURE);
            }
            break;
        case 't':
            num_threads = atoi(optarg);
            if (num_threads < 1 || num_threads > NUM_THREADS)
            {
                fprintf(stderr, "Invalid number of threads: %d\n",
                        num_threads);
                exit(EXIT_FAILURE);
            }
            break;
        case 's':
            hash_size = atoi(optarg);
            if (hash_size <= 0)
            {
                fprintf(stderr, "Invalid hash size: %zu\n", hash_size);
                exit(EXIT_FAILURE);
            }
            break;
        case 'd':
            delay = atoi(optarg);
            if (delay < 0)
            {
                fprintf(stderr, "Invalid rwlock delay: %d\n", delay);
                exit(EXIT_FAILURE);
            }
            break;
        case 'h':
        default:
            fprintf(stdout, "Usage: %s [-p port (%d)] "
                            "[-t num_threads (%d)] "
                            "[-s hash_size (%d)] "
                            "[-d rwlock_delay (%d)]\n",
                    argv[0],
                    DEFAULT_PORT,
                    NUM_THREADS,
                    DEFAULT_HASH_SIZE,
                    RWLOCK_DELAY);
            exit(EXIT_FAILURE);
        }
    }

    /*----------------------------------------------------------------*/
    int listenfd, optval = 1, i;
    struct sockaddr_in addr;
    struct timeval to;
    struct skvs_ctx *ctx = NULL;
    pthread_t threads[NUM_THREADS];
    int created = 0;
    struct sigaction sa;

    /* Don't die on writing to a closed connection. */
    signal(SIGPIPE, SIG_IGN);

    /* Install SIGINT handler (no SA_RESTART so blocking calls return). */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    /* Initialize the SKVS context (global hash table). */
    ctx = skvs_init(hash_size, delay);
    if (!ctx)
    {
        fprintf(stderr, "Failed to initialize SKVS context\n");
        exit(EXIT_FAILURE);
    }

    /* Create the shared listening socket. */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket");
        skvs_destroy(ctx, 0);
        exit(EXIT_FAILURE);
    }
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval));

    /* Timeout on the listening socket so accept() polls g_shutdown. */
    to.tv_sec = TIMEOUT;
    to.tv_usec = 0;
    setsockopt(listenfd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* 0.0.0.0 */
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(listenfd);
        skvs_destroy(ctx, 0);
        exit(EXIT_FAILURE);
    }
    if (listen(listenfd, NUM_BACKLOG) < 0)
    {
        perror("listen");
        close(listenfd);
        skvs_destroy(ctx, 0);
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "Server listening on %s:%d\n", ip, port);
    fflush(stdout);

    /* Spawn the static worker thread pool sharing the listen fd. */
    for (i = 0; i < num_threads; i++)
    {
        struct thread_args *targs = malloc(sizeof(struct thread_args));
        if (!targs)
        {
            fprintf(stderr, "Failed to allocate thread args\n");
            break;
        }
        targs->listenfd = listenfd;
        targs->idx = i;
        targs->ctx = ctx;

        if (pthread_create(&threads[i], NULL, handle_client, targs) != 0)
        {
            perror("pthread_create");
            free(targs);
            break;
        }
        created++;
    }

    /* Wait for all workers to finish (they exit on SIGINT). */
    for (i = 0; i < created; i++)
        pthread_join(threads[i], NULL);

    /* Dump the global hash table, then clean up. */
    close(listenfd);
    skvs_destroy(ctx, 1);

    return 0;
    /*----------------------------------------------------------------*/
}
/*--------------------------------------------------------------------*/