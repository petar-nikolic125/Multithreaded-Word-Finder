#ifndef CONFIG_H
#define CONFIG_H

// Number of buckets in the initial hash map
#define DEFAULT_BUCKETS      4096

// Load factor threshold for triggering rehash (> LOAD_FACTOR_REHASH × buckets)
#define LOAD_FACTOR_REHASH   4

// Capacity of the circular buffer for the job queue
#define QUEUE_CAPACITY       128

// Number of worker threads: 0 ⇒ use sysconf(_SC_NPROCESSORS_ONLN)
#define DEFAULT_NTHREADS     0

// Timeout in seconds after which jq_push logs back-pressure warning
#define QUEUE_BLOCK_TIMEOUT  1.0

#endif // CONFIG_H
