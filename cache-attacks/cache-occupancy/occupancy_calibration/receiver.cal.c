#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <immintrin.h>
#include <string.h>

#include <signal.h>
#include <sys/wait.h>

#include "cacheutils.h"
#include "utils.h"

#define L3_SIZE 4 * 1024 * 1024 // 4MB

size_t COUNTER_THRESHOLD = 910000;

uint64_t FREQ; // cpu TCS frequency
float SCALE_FACTOR; // scale factor to correct rdtsc().
                    // This is because rdtsc tics count is normalized across
                    // cpu cores and to calculate time from it we need the 
                    // normalizing frequency too.

// Array about the size of L3 cache
uint8_t cache_array[L3_SIZE];

// Inline xorshift RNG
// Avoids the overhead of calling rand()
static inline __attribute__((always_inline)) size_t fast_rand() {
    static size_t state = 2463534242UL; // Seed value
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Convert CPU cycles to microseconds. rdtsc_to_us & scaled_time are not needed,
// but I realized that too late so I will let it be.
static inline uint64_t __attribute__ ((always_inline)) rdtsc_to_us(uint64_t start, uint64_t freq) {
    return (start * 1000000ULL) / freq;  // Convert CPU cycles to µs
}

static inline uint64_t __attribute__ ((always_inline))
scaled_time(uint64_t time, float scale_factor) {
    return (uint64_t) ((float) time * scale_factor);
}

// Get CPU TSC frequency (run once)
uint64_t get_cpu_freq() {
    struct timespec ts1, ts2;
    uint64_t start, end;

    clock_gettime(CLOCK_MONOTONIC, &ts1);
    start = __rdtsc();
    usleep(100000);
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    end = __rdtsc();

    uint64_t elapsed_ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL
                        + (ts2.tv_nsec - ts1.tv_nsec);

    return (end - start) * 1000000000ULL / elapsed_ns;
}

// Observe cache. Some care has been taken to run it exactly for SIG_DURATION
// milliseconds by inlining important function calls and de-normalizing 
// time as measured by rdtsc
size_t observe_cache(void) {
    size_t counter = 0;

    volatile uint8_t temp = 0;
    size_t run_duration = SIG_DURATION * 1000; // in microsesc

    uint64_t start_tics = __rdtsc(); // intel intrinsic
    while (rdtsc_to_us(__rdtsc() - start_tics, FREQ) < scaled_time(run_duration, SCALE_FACTOR)) {
        _mm_mfence();
        temp += cache_array[fast_rand() % L3_SIZE];
        _mm_mfence();
        counter++;
    }
    return counter;
}

int main() {
    FREQ = get_cpu_freq();  // get current cpu freq
    uint64_t start = __rdtsc();
    usleep(1000);
    uint64_t elapsed = __rdtsc() - start;
    SCALE_FACTOR = (float) rdtsc_to_us(elapsed, FREQ) / 1000;

    fprintf(stderr, "[RECEIVER] Press any key to initiate listening...\n");

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[RECEIVER] Received EOF key. Exiting.\n");
        exit(-1);
    }

    fprintf(stderr, "[RECEIVER] Listening...press enter at sender side\n");

    size_t counter_values[UP_DOWNS * 8] = { 0 };

    for (int i = 0; i < UP_DOWNS * 8; i++) {
        size_t ctr = observe_cache();
        counter_values[i] = ctr;
    }

    FILE *fp = fopen("counter_values.txt", "w");

    for (int i = 0; i < UP_DOWNS * 8; i++) {
        printf("i: %d, counter: %zu\n", i, counter_values[i]);
        fprintf(fp, "i: %d, counter: %zu\n", i, counter_values[i]);
    }

    fclose(fp);

    printf("results stored in ./counter_values.txt, use the following gnuplot \
command to plot the counter values: \
`plot \"counter_values.txt\" using 2:4 title \"Counter Values\"\n");

    printf("Near the end the counter values will most likely be clustered around a high and a \
low value, if you don't see such then you probably delayed pressing \
enter on sender side. Try again! or increase the UP_DOWNS value in utils.h.\n");
}
