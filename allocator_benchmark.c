#include "el_malloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define HEAP_SIZE (16 * 1024 * 1024)
#define NUM_SLOTS 4096
#define NUM_OPS 100000

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*
 * Deterministic pseudo-random generator so the benchmark is repeatable.
 */
static uint32_t rng_state = 0x12345678u;

static uint32_t next_rand(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

int main(void) {
    void *slots[NUM_SLOTS] = {0};
    size_t live_blocks = 0;
    size_t malloc_count = 0;
    size_t free_count = 0;
    uint64_t malloc_time = 0;
    uint64_t free_time = 0;

    if (el_init(HEAP_SIZE) != 0) {
        fprintf(stderr, "Failed to initialize allocator\n");
        return 1;
    }

    /*
     * Warm up the allocator so the benchmark is measuring allocator
     * behavior rather than the first call alone.
     */
    for (int i = 0; i < 1000; i++) {
        size_t slot = next_rand() % NUM_SLOTS;
        size_t size = 32 + (next_rand() % 481);

        if (slots[slot] == NULL) {
            uint64_t start = now_ns();
            slots[slot] = el_malloc(size);
            malloc_time += now_ns() - start;

            if (slots[slot] != NULL) {
                live_blocks++;
                malloc_count++;
            }
        } else {
            uint64_t start = now_ns();
            el_free(slots[slot]);
            free_time += now_ns() - start;

            slots[slot] = NULL;
            live_blocks--;
            free_count++;
        }
    }

    /*
     * Main benchmark:
     * roughly 60% of operations allocate and 40% free. Because blocks
     * have different sizes and are freed in a non-sequential pattern,
     * this creates fragmentation in the available list.
     */
    for (int i = 0; i < NUM_OPS; i++) {
        size_t slot = next_rand() % NUM_SLOTS;

        if (slots[slot] == NULL) {
            size_t size = 16 + (next_rand() % 1009);

            uint64_t start = now_ns();
            void *ptr = el_malloc(size);
            malloc_time += now_ns() - start;

            if (ptr != NULL) {
                slots[slot] = ptr;
                live_blocks++;
                malloc_count++;
            }
        } else if ((next_rand() % 10) < 4) {
            uint64_t start = now_ns();
            el_free(slots[slot]);
            free_time += now_ns() - start;

            slots[slot] = NULL;
            live_blocks--;
            free_count++;
        }
    }

    printf("\n===== Explicit List Allocator Benchmark =====\n");
    printf("Operations:       %d\n", NUM_OPS);
    printf("Heap size:        %d MB\n", HEAP_SIZE / (1024 * 1024));
    printf("Malloc calls:     %zu\n", malloc_count);
    printf("Free calls:       %zu\n", free_count);
    printf("Live blocks:      %zu\n", live_blocks);

    if (malloc_count > 0) {
        printf("Total malloc:     %llu ns\n",
               (unsigned long long)malloc_time);
        printf("Avg malloc:       %.2f ns\n",
               (double)malloc_time / malloc_count);
        printf("Malloc throughput: %.2f calls/sec\n",
               (double)malloc_count / ((double)malloc_time / 1e9));
    }

    if (free_count > 0) {
        printf("Total free:       %llu ns\n",
               (unsigned long long)free_time);
        printf("Avg free:         %.2f ns\n",
               (double)free_time / free_count);
    }

    /*
     * Clean up blocks still allocated by the benchmark.
     */
    for (size_t i = 0; i < NUM_SLOTS; i++) {
        if (slots[i] != NULL) {
            el_free(slots[i]);
        }
    }

    el_cleanup();
    return 0;
}
