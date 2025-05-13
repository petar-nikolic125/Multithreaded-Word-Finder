#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "config.h"
#include "job_queue.h"
#include "thread_pool.h"
#include "search_engine.h"
#include "util.h"

// globals for signal‐handler
static ThreadPool g_pool;
static JobQueue   g_queue;

// Ctrl-C handler: kick off queue shutdown so workers wake up
static void handle_signal(int sig) {
    (void)sig;
    jq_shutdown(&g_queue);
}

// Tear everything down and start fresh (for a _clear_ command)
static void rebuild_index(HashMap **map_ptr) {
    tp_destroy(&g_pool);
    jq_destroy(&g_queue);
    free_hash_map(*map_ptr);

    *map_ptr = create_hash_map(0);
    jq_init(&g_queue, 0);
    tp_init(&g_pool, DEFAULT_NTHREADS, &g_queue);
}

int main(int argc, char **argv) {
    // 1) Load censored list if provided
    CensoredSet *censored = NULL;
    if (argc > 1) {
        censored = load_censored_set(argv[1]);
        if (!censored) {
            fprintf(stderr,
                    "Warning: failed to load censored set from %s\n\n",
                    argv[1]);
        }
    }
    size_t censor_count = censored_set_count(censored);
    printf("Loaded %zu censored word%s.\n\n",
           censor_count, censor_count==1?"":"s");

    // 2) Print header
    printf("Search Engine Simulator (OS2025 – Domaci 4)\n");
    printf("Commands:\n");
    printf("  _index_  <file>\n");
    printf("  _search_ <word>\n");
    printf("  _clear_\n");
    printf("  _stop_\n\n");

    // 3) Build index infra
    HashMap *map = create_hash_map(0);
    jq_init(&g_queue, 0);
    tp_init(&g_pool, DEFAULT_NTHREADS, &g_queue);

    // 4) Install ctrl-C handler
    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // 5) CLI loop
    char line[256];
    while (1) {
        printf("> ");
        if (!fgets(line, sizeof(line), stdin))
            break;
        trim_nl(line);

        // —— INDEX ——
        if (strncmp(line, "_index_ ", 8) == 0) {
            char *path = line + 8;
            printf("_index_ %s\n", path);
            tp_submit(&g_pool, path, map, censored);
            printf("→ Queued indexing for file: %s\n\n", path);
        }
        // —— SEARCH ——
        else if (strncmp(line, "_search_ ", 9) == 0) {
            char *term = line + 9;
            printf("_search_ %s\n", term);
            printf("→ Searching for: '%s'\n", term);

            if (censored && is_censored(censored, term)) {
                printf("  [!] Search term '%s' is censored.\n\n", term);
            } else {
                search_word(map, term);
                printf("\n");
            }
        }
        // —— CLEAR ——
        else if (strcmp(line, "_clear_") == 0) {
            printf("_clear_\n");
            rebuild_index(&map);
            printf("→ Index has been cleared. All data dropped.\n\n");
        }
        // —— STOP ——
        else if (strcmp(line, "_stop_") == 0) {
            puts("_stop_");
            puts("Stop command received. Shutting down...");
            jq_shutdown(&g_queue);

            puts("Waiting for worker threads to complete...");
            tp_destroy(&g_pool);
            puts("All workers finished.");

            puts("Cleaning up resources...");
            jq_destroy(&g_queue);
            free_hash_map(map);
            free_censored_set(censored);

            puts("Application stopped.");
            return 0;
        }
        // —— UNKNOWN ——
        else {
            printf("  [!] Unknown command: %s\n", line);
            printf("      Try: _index_ <file>, _search_ <word>, _clear_, or _stop_\n\n");
        }
    }

    // (In case of EOF or error fall‐through)
    puts("EOF reached. Shutting down...");
    jq_shutdown(&g_queue);
    tp_destroy(&g_pool);
    jq_destroy(&g_queue);
    free_hash_map(map);
    free_censored_set(censored);
    puts("Application stopped.");
    return 0;
}
