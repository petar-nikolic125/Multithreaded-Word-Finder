#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "util.h"

// --------------------------------------------------------------------------------
// CENSORED SET (concrete definition for the opaque typedef in util.h)
// --------------------------------------------------------------------------------
struct CensoredSet {
    char  **words;  // array of strdup’d tokens
    size_t  count;  // number of entries in `words[]`
};

CensoredSet *load_censored_set(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("load_censored_set");
        return NULL;
    }
    CensoredSet *set = calloc(1, sizeof(*set));
    if (!set) { fclose(f); return NULL; }

    char buf[256];
    while (fscanf(f, "%255s", buf) == 1) {
        char **tmp = realloc(set->words, (set->count + 1) * sizeof(*tmp));
        if (!tmp) break;
        set->words = tmp;
        set->words[set->count++] = strdup(buf);
    }
    fclose(f);
    return set;
}

bool is_censored(const CensoredSet *set, const char *word) {
    if (!set) return false;
    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->words[i], word) == 0)
            return true;
    }
    return false;
}

void free_censored_set(CensoredSet *set) {
    if (!set) return;
    for (size_t i = 0; i < set->count; i++)
        free(set->words[i]);
    free(set->words);
    free(set);
}

// --------------------------------------------------------------------------------
// CENSORED‐SET ACCESSOR
// --------------------------------------------------------------------------------
size_t censored_set_count(const CensoredSet *set) {
    return set ? set->count : 0;
}

// ------------------------
// UTILITIES
// ------------------------
void trim_nl(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) {
        s[--len] = '\0';
    }
}

// ------------------------
// TOKENIZATION
// ------------------------
void tokenize_file(const char        *filepath,
                   HashMap           *map,
                   const CensoredSet *censored)
{
    /* 1. Load entire file into a buffer */
    FILE *f = fopen(filepath, "r");
    if (!f) { perror("tokenize_file: open"); return; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    char *buf = malloc(sz + 1);
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        /* short read → still usable, continue */ ;
    }
    buf[sz] = '\0';
    fclose(f);

    /* 2. Walk buffer sentence-by-sentence ( . ? ! ) */
    for (char *p = buf; *p; ) {

        /* (a) skip leading spaces / newlines */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* (b) start of sentence */
        char *sent_start = p;

        /* (c) advance to first '.', '?', '!' (end of sentence) */
        while (*p && *p!='.' && *p!='?' && *p!='!') p++;
        if (!*p) break;       /* incomplete sentence at EOF */

            p++;                  /* include the terminator       */
            size_t sent_len = (size_t)(p - sent_start);

        /* (d) copy out immutable, newline-collapsed context */
        char *ctx = malloc(sent_len + 1);
        memcpy(ctx, sent_start, sent_len);
        ctx[sent_len] = '\0';
        for (char *q = ctx; *q; q++) {
            if (*q == '\n' || *q == '\r')
                *q = ' ';
        }

            /* 3. If ctx contains a censored word ► skip whole sentence */
            bool skip = false;
        for (char *w = ctx; *w && !skip; ) {
            while (*w && !isalpha((unsigned char)*w)) w++;
            if (!*w) break;
            char *wstart = w;
            while (*w && isalpha((unsigned char)*w)) w++;
            char saved = *w;  *w = '\0';
            if (is_censored(censored, wstart)) skip = true;
            *w = saved;
        }

        /* 4. Index every word *without ever mutating ctx* */
        if (!skip) {
            for (char *w = ctx; *w; ) {
                while (*w && !isalpha((unsigned char)*w)) w++;
                if (!*w) break;
                char *wstart = w;
                while (*w && isalpha((unsigned char)*w)) w++;
                size_t wlen = (size_t)(w - wstart);

                /* make our own null-terminated copy of the word */
                char *word = strndup(wstart, wlen);
                if (word) {
                    add_word_occurrence(map, word, filepath, ctx);
                    free(word);
                }
            }
        }
        free(ctx);
    }

    free(buf);
}

