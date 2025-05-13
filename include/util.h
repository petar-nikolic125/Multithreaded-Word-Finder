#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdbool.h>
#include "search_engine.h"  // for HashMap

// ----------------------------------------------------------------------------
// CENSORED WORD SET
// ----------------------------------------------------------------------------

// Opaque type; actual fields live in util.c only.
typedef struct CensoredSet CensoredSet;

// Load a whitespace‐delimited list of censored words from `filepath`.
// Returns a newly‐allocated CensoredSet, or NULL on error.
CensoredSet *load_censored_set(const char *filepath);

// Returns true if `word` is in `set`.  Safe to call with set==NULL.
bool is_censored(const CensoredSet *set, const char *word);

// Free all memory held by the set.
void free_censored_set(CensoredSet *set);


// ----------------------------------------------------------------------------
// TOKENIZATION & FILE I/O
// ----------------------------------------------------------------------------

// Read the file at `filepath` line‐by‐line; skip any line containing a
// censored word, otherwise index every word in the line using the
// entire line as context.
void tokenize_file(const char        *filepath,
                   HashMap           *map,
                   const CensoredSet *censored);

// Trim trailing newline or carriage‐return from `s` in‐place.
void trim_nl(char *s);


// ----------------------------------------------------------------------------
// CENSORED‐SET ACCESSOR
// ----------------------------------------------------------------------------

// Return the number of words in the set (0 if set==NULL).
size_t censored_set_count(const CensoredSet *set);

#endif // UTIL_H
