//search_engine.h
#ifndef SEARCH_ENGINE_H
#define SEARCH_ENGINE_H

/* Enable POSIX-2008 APIs (including pthread_rwlock_t) */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE   700
#endif

#include <stddef.h>    // for size_t
#include <stdint.h>    // for uint64_t
#include <pthread.h>   // for pthread_rwlock_t, pthread_mutex_t

// -----------------------------------------------------------------------------
// WORD OCCURRENCE & HASH MAP TYPES
// -----------------------------------------------------------------------------

// Represents one occurrence of a word in a file, with context.
typedef struct {
    char *filename;   // file where the word appeared
    char *context;    // the surrounding sentence or snippet
    int   count;      // number of times this snippet appears
} WordOccurrence;

// A single entry in the hash map: one word + dynamic array of occurrences.
typedef struct HashEntry {
    char            *word;      // the indexed word
    WordOccurrence  *occ;       // dynamic array of occurrences
    int              occ_cnt;   // number stored
    int              occ_cap;   // capacity of occ[]
    struct HashEntry *next;     // next in this bucket’s chain
} HashEntry;

// One hash-bucket: head of chain + per-bucket readers–writer lock.
typedef struct {
    HashEntry       *head;
    pthread_rwlock_t lock;      // write-lock for inserts, read-lock for searches
} HashBucket;

// The overall hash map.
typedef struct {
    HashBucket     *buckets;      // array of size cap
    size_t           cap;         // number of buckets
    size_t           n_items;     // total distinct words
    pthread_mutex_t  resize_lock; // guards rehash
} HashMap;

// -----------------------------------------------------------------------------
// PUBLIC API
// -----------------------------------------------------------------------------

// Create a new hash map. If cap==0, uses DEFAULT_BUCKETS from config.h.
HashMap *create_hash_map(size_t cap);

// Insert one occurrence of ‘word’ (from ‘filename’ with ‘context’) into m.
void add_word_occurrence(HashMap *m,
                         const char *word,
                         const char *filename,
                         const char *context);

// Retrieve all occurrences of ‘word’ into a newly-malloc’d array.
// *out_n will be set to the number of entries. Returns NULL if not found.
WordOccurrence *get_word_occurrences(HashMap *m,
                                     const char *word,
                                     int *out_n);

// Free entire hash map and all its allocations.
void free_hash_map(HashMap *m);

// qsort comparator: descending by .count
int cmp_occ(const void *a, const void *b);

// Search for ‘word’ in m and print results to stdout.
void search_word(HashMap *m, const char *word);

#endif // SEARCH_ENGINE_H
