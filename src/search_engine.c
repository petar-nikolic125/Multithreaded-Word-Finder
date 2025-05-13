#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "search_engine.h"
#include "config.h"

// FNV-1a 64-bit hash for strings
static inline uint64_t fnv1a(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

HashMap *create_hash_map(size_t cap) {
    HashMap *m = calloc(1, sizeof(*m));
    m->cap     = cap ? cap : DEFAULT_BUCKETS;
    m->n_items = 0;
    m->buckets = calloc(m->cap, sizeof(*m->buckets));
    pthread_mutex_init(&m->resize_lock, NULL);
    for (size_t i = 0; i < m->cap; i++)
        pthread_rwlock_init(&m->buckets[i].lock, NULL);
    return m;
}

void add_word_occurrence(HashMap *m,
                         const char *word,
                         const char *filename,
                         const char *context)
{
    uint64_t idx = fnv1a(word) % m->cap;
    pthread_rwlock_wrlock(&m->buckets[idx].lock);

    // find or create entry
    HashEntry *e = m->buckets[idx].head;
    while (e && strcmp(e->word, word) != 0) e = e->next;
    if (!e) {
        e = calloc(1, sizeof(*e));
        e->word    = strdup(word);
        e->occ_cap = 4;
        e->occ     = malloc(e->occ_cap * sizeof(*e->occ));
        e->occ_cnt = 0;
        e->next    = m->buckets[idx].head;
        m->buckets[idx].head = e;
        __sync_add_and_fetch(&m->n_items, 1);
    }

    // append one occurrence
    if (e->occ_cnt == e->occ_cap) {
        e->occ_cap <<= 1;
        e->occ = realloc(e->occ, e->occ_cap * sizeof(*e->occ));
    }
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
    while (e && strcmp(e->word, word) != 0) e = e->next;

    WordOccurrence *res = NULL;
    if (e) {
        *out_n = e->occ_cnt;
        res    = malloc(e->occ_cnt * sizeof(*res));
        memcpy(res, e->occ, e->occ_cnt * sizeof(*res));
    }
    pthread_rwlock_unlock(&m->buckets[idx].lock);
    return res;
}

void free_hash_map(HashMap *m) {
    for (size_t i = 0; i < m->cap; i++) {
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
        pthread_rwlock_destroy(&m->buckets[i].lock);
    }
    free(m->buckets);
    pthread_mutex_destroy(&m->resize_lock);
    free(m);
}

// Comparator by filename (for grouping)
static int cmp_by_fname(const void *A, const void *B) {
    const WordOccurrence *x = A, *y = B;
    int c = strcmp(x->filename, y->filename);
    if (c) return c;

    return strcmp(x->context, y->context);
}
void search_word(HashMap *m, const char *word) {
    int total = 0;
    WordOccurrence *occ = get_word_occurrences(m, word, &total);
    if (!occ || total == 0) {
        printf("No results for '%s'.\n\n", word);
        free(occ);
        return;
    }

    // group them by filename
    qsort(occ, total, sizeof(*occ), cmp_by_fname);

    // header
    printf("Search results for '%s':\n", word);

    int i = 0;
    while (i < total) {
        const char *fname = occ[i].filename;
        int start = i;
        while (i < total && strcmp(occ[i].filename, fname) == 0)
            i++;
        int count = i - start;

        printf("File: %s (found %d %s)\n",
               fname,
               count,
               count==1 ? "time" : "times");
        printf("Contexts:\n");
        for (int j = start; j < i; j++) {
            printf(" - \"%s\"\n", occ[j].context);
        }
        printf("\n");
    }

    free(occ);
}
