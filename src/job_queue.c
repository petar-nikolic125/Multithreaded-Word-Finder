//job_queue.c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "job_queue.h"
#include "config.h"

// Initialize queue with capacity (if cap==0, use QUEUE_CAPACITY)
void jq_init(JobQueue *q, size_t cap) {
    q->cap   = cap ? cap : QUEUE_CAPACITY;
    q->buf   = calloc(q->cap, sizeof(Job));
    q->head  = q->tail = 0;
    q->closed = false;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

// Push a job; block if full, logging if blocked > QUEUE_BLOCK_TIMEOUT
void jq_push(JobQueue *q, Job j) {
    pthread_mutex_lock(&q->mtx);
    struct timespec ts;
    while ((q->tail + 1) % q->cap == q->head) {
        // compute absolute timeout
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += (time_t)QUEUE_BLOCK_TIMEOUT;
        ts.tv_nsec += (long)((QUEUE_BLOCK_TIMEOUT - (time_t)QUEUE_BLOCK_TIMEOUT) * 1e9);
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        int rc = pthread_cond_timedwait(&q->not_full, &q->mtx, &ts);
        if (rc == ETIMEDOUT) {
            fprintf(stderr,
                    "Warning: job queue push blocked > %.1f seconds\n",
                    QUEUE_BLOCK_TIMEOUT);
        }
    }
    q->buf[q->tail] = j;
    q->tail = (q->tail + 1) % q->cap;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mtx);
}

// Pop next job into *out; return false if queue closed and empty
bool jq_pop(JobQueue *q, Job *out) {
    pthread_mutex_lock(&q->mtx);
    while (q->head == q->tail && !q->closed) {
        pthread_cond_wait(&q->not_empty, &q->mtx);
    }
    if (q->head == q->tail && q->closed) {
        pthread_mutex_unlock(&q->mtx);
        return false;
    }
    *out = q->buf[q->head];
    q->head = (q->head + 1) % q->cap;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
    return true;
}

// Mark queue as closed and wake all waiters
void jq_shutdown(JobQueue *q) {
    pthread_mutex_lock(&q->mtx);
    q->closed = true;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mtx);
}

// Destroy queue: free buffer and destroy sync primitives
void jq_destroy(JobQueue *q) {
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    free(q->buf);
}
