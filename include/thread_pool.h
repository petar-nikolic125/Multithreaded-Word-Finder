#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>     // size_t
#include <stdbool.h>    // bool
#include <pthread.h>    // pthread_t

#include "job_queue.h"    // JobQueue
#include "search_engine.h"// HashMap  (for dedup set)
#include "util.h"         // CensoredSet

// Fixed-size pool of worker threads consuming Jobs from a JobQueue
typedef struct {
    pthread_t  *workers;   // thread IDs
    size_t      n;         // number of threads
    JobQueue   *queue;     // shared queue
} ThreadPool;

// Start n_threads workers (0 ⇒ #CPU cores) pulling from `q`
void tp_init(ThreadPool *pool, size_t n_threads, JobQueue *q);

/*  Enqueue one file-to-index job.
 *  Returns true  – job pushed
 *          false – file was already queued/indexed OR allocation error.
 */
bool tp_submit(ThreadPool   *pool,
               const char   *filename,
               HashMap      *map,
               CensoredSet  *censored);

// Gracefully signal shutdown, join workers & free resources
void tp_destroy(ThreadPool *pool);

#endif // THREAD_POOL_H
