#define _POSIX_C_SOURCE 200809L  // for sysconf
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "thread_pool.h"
#include "util.h"          // for tokenize_file(), CensoredSet
#include "search_engine.h" // for HashMap, dedup fields
#include "config.h"

// mutex for synchronized terminal output
static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;


 // Deduplication helper – uses HashMap’s indexed_files[] set

static bool already_indexed(HashMap *m, const char *path)
{
    pthread_mutex_lock(&m->file_set_lock);

    /* check if already recorded */
    for (size_t i = 0; i < m->n_files; ++i) {
        if (strcmp(m->indexed_files[i], path) == 0) {
            pthread_mutex_unlock(&m->file_set_lock);
            return true;
        }
    }

    /* grow array if needed */
    if (m->n_files == m->cap_files) {
        size_t new_cap = m->cap_files ? m->cap_files * 2 : 8;
        char **tmp = realloc(m->indexed_files, new_cap * sizeof(*tmp));
        if (!tmp) {
            perror("already_indexed: realloc");
        } else {
            m->indexed_files = tmp;
            m->cap_files     = new_cap;
        }
    }

    /* record new path */
    m->indexed_files[m->n_files++] = strdup(path);

    pthread_mutex_unlock(&m->file_set_lock);
    return false;
}

/* --------------------------------------------------------------------------
 *  Worker thread
 * --------------------------------------------------------------------------*/
static void *worker_fn(void *arg)
{
    JobQueue *q = (JobQueue *)arg;
    Job job;

    while (jq_pop(q, &job)) {
        errno = 0;
        tokenize_file(job.filename, job.map, job.censored);
        int err = errno;

        pthread_mutex_lock(&log_mtx);
        if (err) {
            fprintf(stderr,
                    "Error: tokenize_file failed for '%s': %s\n",
                    job.filename, strerror(err));
        } else {
            printf("Worker finished indexing: %s\n", job.filename);
            fflush(stdout);
        }
        pthread_mutex_unlock(&log_mtx);

        free(job.filename);
    }
    return NULL;
}

void tp_init(ThreadPool *pool, size_t n_threads, JobQueue *q)
{
    size_t n = n_threads
    ? n_threads
    : (size_t)sysconf(_SC_NPROCESSORS_ONLN);

    pool->n       = n;
    pool->queue   = q;
    pool->workers = malloc(n * sizeof(pthread_t));
    if (!pool->workers) {
        perror("tp_init: malloc");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < n; ++i) {
        if (pthread_create(&pool->workers[i], NULL, worker_fn, q) != 0) {
            perror("tp_init: pthread_create");
            exit(EXIT_FAILURE);
        }
    }
}

bool tp_submit(ThreadPool  *pool,
               const char  *filename,
               HashMap     *map,
               CensoredSet *censored)
{
    /* skip duplicates */
    if (already_indexed(map, filename)) {
        pthread_mutex_lock(&log_mtx);
        printf("→ File already queued/indexed: %s\n", filename);
        pthread_mutex_unlock(&log_mtx);
        return false;
    }

    char *copy = strdup(filename);
    if (!copy) {
        perror("tp_submit: strdup");
        return false;
    }

    Job j = {
        .filename = copy,
        .map      = map,
        .censored = censored
    };
    jq_push(pool->queue, j);
    return true;
}

void tp_destroy(ThreadPool *pool)
{
    jq_shutdown(pool->queue);
    for (size_t i = 0; i < pool->n; ++i) {
        pthread_join(pool->workers[i], NULL);
    }
    free(pool->workers);
}
