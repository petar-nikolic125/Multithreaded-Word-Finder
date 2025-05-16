#define _POSIX_C_SOURCE 200809L  // for sysconf, timespec_get
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

#include "search_engine.h"
#include "config.h"

#define MAX_LOAD_FACTOR 0.75

// ANSI escape codes for styling
#define BOLD       "\033[1m"
#define UNDERLINE  "\033[4m"
#define RESET      "\033[0m"
#define CYAN       "\033[36m"
#define GREEN      "\033[32m"
#define RED        "\033[31m"
#define GRAY       "\033[90m"

// FNV-1a 64-bit hash for strings
static inline uint64_t fnv1a(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static void resize_map(HashMap *m)
{
    const size_t old_cap = m->cap;
    HashBucket  *old     = m->buckets;

    const size_t new_cap = old_cap * 2;
    HashBucket  *newb    = calloc(new_cap, sizeof *newb);
    if (!newb) { perror("resize_map: calloc"); return; }

    /* 0.  Initialise locks on the new buckets */
    for (size_t i = 0; i < new_cap; ++i) {
        if (pthread_rwlock_init(&newb[i].lock, NULL) != 0) {
            perror("resize_map: rwlock_init");
            /* roll back already-initialised locks */
            while (i--) pthread_rwlock_destroy(&newb[i].lock);
            free(newb);
            return;
        }
    }

    /* 1.  Iterate over the old buckets */
    for (size_t i = 0; i < old_cap; ++i) {
        /* 1a. Lock the old bucket exclusively and detach its chain */
        pthread_rwlock_wrlock(&old[i].lock);
        HashEntry *chain = old[i].head;
        old[i].head      = NULL;              /* bucket is now empty      */
        pthread_rwlock_unlock(&old[i].lock);

        /* 1b. Re-insert every node into the new table */
        while (chain) {
            HashEntry *next = chain->next;
            uint64_t   idx  = fnv1a(chain->word) % new_cap;

            pthread_rwlock_wrlock(&newb[idx].lock);
            chain->next      = newb[idx].head;
            newb[idx].head   = chain;
            pthread_rwlock_unlock(&newb[idx].lock);

            chain = next;
        }
        /* 1c. The old bucket is empty – its lock can be destroyed */
        pthread_rwlock_destroy(&old[i].lock);
    }

    /* 2.  Swap in the new table */
    free(old);
    m->buckets = newb;
    m->cap     = new_cap;
}

// Check current load factor and resize if needed
static void try_resize(HashMap *m) {
    double load = (double)atomic_load(&m->n_items) / (double)m->cap;
    if (load < MAX_LOAD_FACTOR) return;

    pthread_mutex_lock(&m->resize_lock);
    load = (double)atomic_load(&m->n_items) / (double)m->cap;
    if (load >= MAX_LOAD_FACTOR) {
        resize_map(m);
    }
    pthread_mutex_unlock(&m->resize_lock);
}

HashMap *create_hash_map(size_t cap) {
    HashMap *m = calloc(1, sizeof(*m));
    if (!m) {
        perror("create_hash_map: calloc");
        exit(EXIT_FAILURE);
    }
    m->cap          = cap ? cap : DEFAULT_BUCKETS;
    atomic_init(&m->n_items, 0);
    m->buckets      = calloc(m->cap, sizeof(*m->buckets));
    if (!m->buckets) {
        perror("create_hash_map: calloc buckets");
        free(m);
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&m->resize_lock, NULL);

    // init file deduplication set
    pthread_mutex_init(&m->file_set_lock, NULL);
    m->indexed_files = NULL;
    m->n_files       = 0;
    m->cap_files     = 0;

    for (size_t i = 0; i < m->cap; i++) {
        pthread_rwlock_init(&m->buckets[i].lock, NULL);
    }
    return m;
}

void add_word_occurrence(HashMap *m,
                         const char *word,
                         const char *filename,
                         const char *context)
{
    try_resize(m);

    uint64_t idx = fnv1a(word) % m->cap;
    pthread_rwlock_wrlock(&m->buckets[idx].lock);

    // find or create entry
    HashEntry *e = m->buckets[idx].head;
    while (e && strcmp(e->word, word) != 0) {
        e = e->next;
    }
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) {
            perror("add_word_occurrence: calloc entry");
            pthread_rwlock_unlock(&m->buckets[idx].lock);
            return;
        }
        e->word    = strdup(word);
        e->occ_cap = 4;
        e->occ     = malloc(e->occ_cap * sizeof(*e->occ));
        if (!e->word || !e->occ) {
            perror("add_word_occurrence: strdup/malloc");
            free(e->word);
            free(e->occ);
            free(e);
            pthread_rwlock_unlock(&m->buckets[idx].lock);
            return;
        }
        e->occ_cnt = 0;
        e->next    = m->buckets[idx].head;
        m->buckets[idx].head = e;
        atomic_fetch_add(&m->n_items, 1);
    }

    // merge repeated context
    if (e->occ_cnt > 0) {
        WordOccurrence *last = &e->occ[e->occ_cnt - 1];
        if (strcmp(last->filename, filename) == 0 &&
            strcmp(last->context, context) == 0) {
            last->count++;
        pthread_rwlock_unlock(&m->buckets[idx].lock);
        return;
            }
    }

    // grow occ array if needed
    if (e->occ_cnt == e->occ_cap) {
        size_t new_cap = e->occ_cap * 2;
        WordOccurrence *tmp = realloc(e->occ, new_cap * sizeof(*e->occ));
        if (!tmp) {
            perror("add_word_occurrence: realloc occ");
            pthread_rwlock_unlock(&m->buckets[idx].lock);
            return;
        }
        e->occ     = tmp;
        e->occ_cap = new_cap;
    }

    // append new occurrence
    e->occ[e->occ_cnt++] = (WordOccurrence){
        .filename = strdup(filename),
        .context  = strdup(context),
        .count    = 1
    };

    pthread_rwlock_unlock(&m->buckets[idx].lock);
}

WordOccurrence *get_word_occurrences(HashMap *m,
                                     const char *word,
                                     int *out_n)
{
    uint64_t idx = fnv1a(word) % m->cap;
    pthread_rwlock_rdlock(&m->buckets[idx].lock);
    HashEntry *e = m->buckets[idx].head;
    while (e && strcmp(e->word, word) != 0) {
        e = e->next;
    }

    WordOccurrence *res = NULL;
    if (e) {
        *out_n = e->occ_cnt;
        res    = malloc(e->occ_cnt * sizeof(*res));
        if (res) {
            memcpy(res, e->occ, e->occ_cnt * sizeof(*res));
        } else {
            perror("get_word_occurrences: malloc");
            *out_n = 0;
        }
    }
    pthread_rwlock_unlock(&m->buckets[idx].lock);
    return res;
}

void free_hash_map(HashMap *m) {
    // destroy buckets
    for (size_t i = 0; i < m->cap; i++) {
        pthread_rwlock_destroy(&m->buckets[i].lock);
        HashEntry *e = m->buckets[i].head;
        while (e) {
            HashEntry *tmp = e->next;
            free(e->word);
            for (int j = 0; j < e->occ_cnt; j++) {
                free(e->occ[j].filename);
                free(e->occ[j].context);
            }
            free(e->occ);
            free(e);
            e = tmp;
        }
    }
    free(m->buckets);

    // destroy file_set
    for (size_t i = 0; i < m->n_files; i++) {
        free(m->indexed_files[i]);
    }
    free(m->indexed_files);
    pthread_mutex_destroy(&m->file_set_lock);

    pthread_mutex_destroy(&m->resize_lock);
    free(m);
}

// compare by filename then context
static int cmp_by_fname(const void *A, const void *B) {
    const WordOccurrence *x = A, *y = B;
    int c = strcmp(x->filename, y->filename);
    return c ? c : strcmp(x->context, y->context);
}

void search_word(HashMap *m, const char *word) {
    int total = 0;
    WordOccurrence *occ = get_word_occurrences(m, word, &total);

    if (!occ || total == 0) {
        printf("\n" RED "No results for '%s'." RESET "\n\n", word);
        free(occ);
        return;
    }

    printf("\n" BOLD CYAN "Search results for '%s':" RESET "\n\n", word);

    qsort(occ, total, sizeof(*occ), cmp_by_fname);

    for (int i = 0; i < total; ) {
        const char *fname = occ[i].filename;
        int start = i;
        while (i < total && strcmp(occ[i].filename, fname) == 0) i++;
        int count = i - start;

        printf(BOLD GREEN "File: %s" RESET " " GRAY "(%d×)" RESET "\n",
               fname, count);
        printf("  " BOLD "Contexts:" RESET "\n");
        for (int j = start; j < i; j++) {
            printf("    - \"%s\"\n", occ[j].context);
        }
        putchar('\n');
    }

    free(occ);
}
