#define _POSIX_C_SOURCE 200809L  // for strndup, fileno, etc.
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>    // for strcasecmp
#include <ctype.h>
#include <stdbool.h>
#include "util.h"

// --------------------------------------------------------------------------------
// CENSORED SET (concrete definition for the opaque typedef in util.h)
// --------------------------------------------------------------------------------
struct CensoredSet {
    char  **words;  // array of strdup’d, lowercase tokens
    size_t  count;  // number of entries in `words[]`
};

CensoredSet *load_censored_set(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("load_censored_set: fopen");
        return NULL;
    }
    CensoredSet *set = calloc(1, sizeof(*set));
    if (!set) {
        perror("load_censored_set: calloc");
        fclose(f);
        return NULL;
    }

    char buf[256];
    while (fscanf(f, "%255s", buf) == 1) {
        // lowercase the token
        for (char *p = buf; *p; ++p) {
            *p = (char)tolower((unsigned char)*p);
        }
        char *token = strdup(buf);
        if (!token) {
            perror("load_censored_set: strdup");
            break;
        }
        char **tmp = realloc(set->words, (set->count + 1) * sizeof(*tmp));
        if (!tmp) {
            perror("load_censored_set: realloc");
            free(token);
            break;
        }
        set->words          = tmp;
        set->words[set->count++] = token;
    }

    fclose(f);
    return set;
}

bool is_censored(const CensoredSet *set, const char *word) {
    if (!set) return false;
    // case-insensitive check
    for (size_t i = 0; i < set->count; i++) {
        if (strcasecmp(set->words[i], word) == 0) {
            return true;
        }
    }
    return false;
}

void free_censored_set(CensoredSet *set) {
    if (!set) return;
    for (size_t i = 0; i < set->count; i++) {
        free(set->words[i]);
    }
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
    FILE *f = fopen(filepath, "r");
    if (!f) {
        perror("tokenize_file: fopen");
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        perror("tokenize_file: fseek");
        fclose(f);
        return;
    }
    long sz = ftell(f);
    if (sz < 0) {
        perror("tokenize_file: ftell");
        fclose(f);
        return;
    }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        perror("tokenize_file: malloc");
        fclose(f);
        return;
    }
    size_t nread = fread(buf, 1, (size_t)sz, f);
    buf[nread] = '\0';
    if (nread < (size_t)sz) {
        fprintf(stderr,
                "tokenize_file: warning short read %zu of %ld bytes\n",
                nread, sz);
    }
    fclose(f);

    // Walk buffer sentence-by-sentence (terminators: . ? !)
    for (char *p = buf; *p; ) {
        // skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        // start of sentence
        char *sent_start = p;
        while (*p && *p != '.' && *p != '?' && *p != '!') p++;
        if (!*p) break;
        p++;  // include terminator

        size_t sent_len = (size_t)(p - sent_start);
        char *ctx = malloc(sent_len + 1);
        if (!ctx) {
            perror("tokenize_file: malloc ctx");
            break;
        }
        memcpy(ctx, sent_start, sent_len);
        ctx[sent_len] = '\0';
        // collapse newlines
        for (char *q = ctx; *q; q++) {
            if (*q == '\n' || *q == '\r') *q = ' ';
        }

        // check censored (case-insensitive)
        bool skip = false;
        for (char *w = ctx; *w && !skip; ) {
            while (*w && !isalpha((unsigned char)*w)) w++;
            if (!*w) break;
            char *ws = w;
            while (*w && isalpha((unsigned char)*w)) w++;
            char saved = *w; *w = '\0';
            if (is_censored(censored, ws)) skip = true;
            *w = saved;
        }

        // index words
        if (!skip) {
            for (char *w = ctx; *w; ) {
                while (*w && !isalpha((unsigned char)*w)) w++;
                if (!*w) break;
                char *ws = w;
                while (*w && isalpha((unsigned char)*w)) w++;
                size_t wlen = (size_t)(w - ws);

                char *word = strndup(ws, wlen);
                if (!word) {
                    perror("tokenize_file: strndup");
                    continue;
                }
                add_word_occurrence(map, word, filepath, ctx);
                free(word);
            }
        }

        free(ctx);
    }

    free(buf);
}
