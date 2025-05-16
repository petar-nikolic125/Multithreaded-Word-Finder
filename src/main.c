#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "config.h"
#include "job_queue.h"
#include "thread_pool.h"
#include "search_engine.h"
#include "util.h"

// ANSI styling
#define BOLD  "\033[1m"
#define CYAN  "\033[36m"
#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

static ThreadPool       g_pool;
static JobQueue         g_queue;
static volatile sig_atomic_t terminate = 0;

/* activity-log */
static FILE *logf = NULL;
static size_t count_index = 0, count_search = 0;

/* -------------------------------------------------------------------------- */
static void handle_signal(int sig) { (void)sig; terminate = 1; }

/* -------------------------------------------------------------------------- */
static void rebuild_index(HashMap **map_ptr)
{
    tp_destroy(&g_pool);
    jq_destroy(&g_queue);
    free_hash_map(*map_ptr);

    *map_ptr = create_hash_map(0);
    jq_init(&g_queue, 0);
    tp_init(&g_pool, DEFAULT_NTHREADS, &g_queue);
}

/* -------------------------------------------------------------------------- */
static void cleanup(HashMap *map, CensoredSet *censored)
{
    jq_shutdown(&g_queue);
    tp_destroy(&g_pool);
    jq_destroy(&g_queue);
    free_hash_map(map);
    free_censored_set(censored);

    if (logf) {
        time_t t = time(NULL);
        fprintf(logf, "[%ld] EXIT  indexed=%zu  searched=%zu\n",
                t, count_index, count_search);
        fclose(logf);
    }

    printf("\n" BOLD "Summary:" RESET
    " %zu file(s) indexed, %zu search(es)\n\n",
           count_index, count_search);
    puts("Application stopped.");
}

/* ========================================================================== */
int main(int argc, char **argv)
{
    /* open activity log */
    logf = fopen("activity.log", "a");

    /* 1) censored set --------------------------------------------------------*/
    CensoredSet *censored = NULL;
    if (argc > 1) {
        censored = load_censored_set(argv[1]);
        if (!censored)
            fprintf(stderr, RED "Warning: couldn't load censored list %s\n"
            RESET "\n", argv[1]);
    }
    size_t n_cen = censored_set_count(censored);
    printf("Loaded %zu censored word%s.\n\n", n_cen, n_cen==1?"":"s");
    if (logf) {
        time_t t = time(NULL);
        fprintf(logf, "[%ld] loaded %zu censored words\n", t, n_cen);
    }

    /* 2) banner ------------------------------------------------------------- */
    puts("Search Engine Simulator (OS2025 – Domaci 4)");
    puts("_index_  <file>");
    puts("_search_ <word>");
    puts("_clear_");
    puts("_stop_\n");

    /* 3) infra -------------------------------------------------------------- */
    HashMap *map = create_hash_map(0);
    jq_init(&g_queue, 0);
    tp_init(&g_pool, DEFAULT_NTHREADS, &g_queue);

    /* 4) signals ------------------------------------------------------------ */
    struct sigaction sa = { .sa_handler = handle_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* 5) REPL --------------------------------------------------------------- */
    char line[256];
    while (1) {
        if (terminate) { puts("\nSignal received. Shutting down..."); break; }

        printf("> ");
        if (!fgets(line, sizeof line, stdin)) { puts(""); break; }
        trim_nl(line);

        /* INDEX ------------------------------------------------------------ */
        if (strncmp(line, "_index_ ", 8) == 0) {
            char *path = line + 8;
            time_t now = time(NULL);

            printf("\n" BOLD CYAN "_index_ %s" RESET "\n\n", path);
            if (tp_submit(&g_pool, path, map, censored)) {
                ++count_index;
                printf(GREEN "→ Queued indexing for file: %s" RESET "\n\n", path);
                if (logf) fprintf(logf, "[%ld] index %s\n", now, path);
            }

            /* SEARCH ----------------------------------------------------------- */
        } else if (strncmp(line, "_search_ ", 9) == 0) {
            char *term = line + 9;
            time_t now = time(NULL);

            printf("\n" BOLD CYAN "_search_ %s" RESET "\n\n", term);
            printf(GREEN "→ Searching for: '%s'" RESET "\n\n", term);

            if (censored && is_censored(censored, term)) {
                printf(RED "  [!] Search term '%s' is censored.\n\n" RESET, term);
                if (logf) fprintf(logf, "[%ld] censored %s\n", now, term);
            } else {
                ++count_search;
                if (logf) fprintf(logf, "[%ld] search %s\n", now, term);
                search_word(map, term);
            }

            /* CLEAR ------------------------------------------------------------ */
        } else if (!strcmp(line, "_clear_")) {
            time_t now = time(NULL);
            printf("\n" BOLD CYAN "_clear_" RESET "\n\n");
            rebuild_index(&map);
            printf(GREEN "→ Index cleared — all data dropped.\n\n" RESET);
            if (logf) fprintf(logf, "[%ld] clear\n", now);

            /* STOP ------------------------------------------------------------- */
        } else if (!strcmp(line, "_stop_")) {
            time_t now = time(NULL);
            printf("\n" BOLD CYAN "_stop_" RESET "\n\n");
            printf(GREEN "Stop command received. Shutting down...\n\n" RESET);
            if (logf) fprintf(logf, "[%ld] stop\n", now);
            cleanup(map, censored);
            return 0;

            /* UNKNOWN ---------------------------------------------------------- */
        } else {
            printf(RED "  [!] Unknown command: %s\n" RESET
            "      Try: _index_, _search_, _clear_, or _stop_\n\n", line);
            if (logf) {
                time_t now = time(NULL);
                fprintf(logf, "[%ld] unknown %s\n", now, line);
            }
        }
    }

    cleanup(map, censored);
    return 0;
}
