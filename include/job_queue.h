#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <stdbool.h>
#include <pthread.h>
#include "search_engine.h"  // for HashMap
#include "util.h"           // for CensoredSet

// A unit of work: index this file into the shared map,
// using the provided censored‐word set.
typedef struct {
    char         *filename;  // path to file
    HashMap      *map;       // shared index
    CensoredSet  *censored;  // which words to skip
} Job;

typedef struct {
    Job             *buf;        // circular buffer of Jobs
    size_t           cap;        // total slots
    size_t           head, tail; // next pop / push
    bool             closed;     // set when shutting down
    pthread_mutex_t  mtx;        // protects head/tail/closed
    pthread_cond_t   not_empty;  // signaled when buf goes non-empty
    pthread_cond_t   not_full;   // signaled when buf goes non-full
} JobQueue;

// Initialize queue (cap==0 ⇒ use default QUEUE_CAPACITY)
void jq_init(JobQueue *q, size_t cap);

// Push one job (blocks if full; logs on timeout)
void jq_push(JobQueue *q, Job j);

// Pop one job into *out; returns false if closed & empty
bool jq_pop(JobQueue *q, Job *out);

// Mark queue closed and wake all waiters
void jq_shutdown(JobQueue *q);

// Destroy queue: free buffer & sync primitives
void jq_destroy(JobQueue *q);

#endif // JOB_QUEUE_H
