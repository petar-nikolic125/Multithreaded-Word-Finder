#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

/* POSIX-2008 APIs (pthread_rwlock_t, etc.) */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE   700
#endif

#include <stddef.h>    // size_t
#include <stdint.h>    // uint64_t
#include <pthread.h>   // pthread_rwlock_t, pthread_mutex_t
#include <stdbool.h>   // bool

// ------- Data structures for word indexing -------

// One occurrence of a word, with its file and snippet.
typedef struct {
    char *filename;
    char *context;
    int   count;
} WordOccurrence;

// Hash map entry: a word and its occurrences.
typedef struct HashEntry {
    char            *word;
    WordOccurrence  *occ;       // dynamic array
    int              occ_cnt;
    int              occ_cap;
    struct HashEntry *next;
} HashEntry;

// A bucket holds a chain of entries plus a rwlock.
typedef struct {
    HashEntry       *head;
    pthread_rwlock_t lock;
} HashBucket;

// Hash map with optional file deduplication.
typedef struct {
    HashBucket     *buckets;      // buckets array
    size_t           cap;         // number of buckets
    size_t           n_items;     // distinct words
    pthread_mutex_t  resize_lock; // protects rehash

    // Track already indexed files
    pthread_mutex_t  file_set_lock;
    char           **indexed_files;
    size_t           n_files;
    size_t           cap_files;
} HashMap;

// -------- Public API --------
/** Create a new hash map (use DEFAULT_BUCKETS if cap==0). */
HashMap *create_hash_map(size_t cap);

/** Add one occurrence of word (from filename/context). */
void add_word_occurrence(HashMap *m,
                         const char *word,
                         const char *filename,
                         const char *context);

/**
 * Get all occurrences of word. Returns malloc'd array and sets *out_n,
 * or NULL if word not found.
 */
WordOccurrence *get_word_occurrences(HashMap *m,
                                     const char *word,
                                     int *out_n);

/** Free the map and all data. */
void free_hash_map(HashMap *m);

/** Compare two WordOccurrence by count (desc) for qsort. */
int cmp_occ(const void *a, const void *b);

/** Find word and print its occurrences to stdout. */
void search_word(HashMap *m, const char *word);

#endif // SEARCH_ENGINE_H
