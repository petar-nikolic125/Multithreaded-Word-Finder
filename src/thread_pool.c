#define _GNU_SOURCE        // for sysconf
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "thread_pool.h"
#include "util.h"       // for tokenize_file(), CensoredSet
#include "config.h"

static void *worker_fn(void *arg)
{
    JobQueue *q = (JobQueue *)arg;
    Job job;
    while (jq_pop(q, &job)) {

        if (job.filename && job.map) {
            tokenize_file(job.filename, job.map, job.censored);
            /* 2) User-visible log                                        */
            printf("Worker finished indexing: %s\n", job.filename);
            fflush(stdout);
        } else {
            fprintf(stderr,
                    "worker_fn: received incomplete job (filename=%p, map=%p)\n",
                    (void *)job.filename, (void *)job.map);
        }
        /* 3) Clean-up the strdup’d path that tp_submit() created         */
        free(job.filename);
    }

    return NULL;   /* terminate thread */
}
// Initialize pool: spawn up to n_threads (0 => #CPU cores), attach to q
void tp_init(ThreadPool *pool, size_t n_threads, JobQueue *q) {
    size_t n = (n_threads == 0)
    ? (size_t)sysconf(_SC_NPROCESSORS_ONLN)
    : n_threads;

    pool->n       = n;
    pool->queue   = q;
    pool->workers = malloc(n * sizeof(pthread_t));
    if (!pool->workers) {
        perror("tp_init: malloc");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < n; i++) {
        if (pthread_create(&pool->workers[i],
            NULL,
            worker_fn,
            q) != 0) {
            perror("tp_init: pthread_create");
        exit(EXIT_FAILURE);
            }
    }
}

// Submit one indexing job: copy filename, carry map & censored set
void tp_submit(ThreadPool  *pool,
               const char  *filename,
               HashMap     *map,
               CensoredSet *censored)
{
    char *copy = strdup(filename);
    if (!copy) {
        perror("tp_submit: strdup");
        return;
    }

    Job j = {
        .filename = copy,
        .map      = map,
        .censored = censored
    };
    jq_push(pool->queue, j);
}

// Gracefully destroy pool: signal shutdown, join threads, free workers[]
void tp_destroy(ThreadPool *pool) {
    jq_shutdown(pool->queue);
    for (size_t i = 0; i < pool->n; i++) {
        pthread_join(pool->workers[i], NULL);
    }
    free(pool->workers);
}
