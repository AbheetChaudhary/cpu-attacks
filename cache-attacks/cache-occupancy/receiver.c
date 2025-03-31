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
#include <math.h>

#include <signal.h>
#include <sys/wait.h>

#include "cacheutils.h"
#include "utils.h"

// update if you need more(numbers etc.), keep the final '\0' byte.
#define VALID_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz :.,\n'\0"

size_t COUNTER_THRESHOLD = 120000; // Update this for your machine.
                                   // See ./occupancy_calibration/
                                   // and README.md

#define L3_SIZE 4 * 1024 * 1024 // 4MB // my approximate cache size


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

// Convert CPU cycles to microseconds. This and 'scaled_time' are not needed,
// but I realized it too late, so I'll let it be.
static inline uint64_t __attribute__ ((always_inline)) rdtsc_to_us(uint64_t start, uint64_t freq) {
    return (start * 1000000ULL) / freq;  // Convert CPU cycles to µs
}

// Converts time(real, not normalized) so that is comparable to time 
// inferred from rdtsc
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

// Function to calculate Hamming distance
int hamming_distance(const char *s1, const char *s2) {
    int distance = 0;
    for (int i = 0; i < 8; i++) {
        if (s1[i] != s2[i]) {
            distance++;
        }
    }
    return distance;
}

// Function to find the closest valid character
char find_closest_valid_char(const char *byte_str) {
    char closest_char = '?';
    int min_distance = 8; // Maximum possible Hamming distance for 8-bit values

    for (int i = 0; VALID_CHARS[i] != '\0'; i++) {
        char valid_char = VALID_CHARS[i];
        char char_bin[9];
        snprintf(char_bin, 9, "%08b", valid_char);

        int distance = hamming_distance(byte_str, char_bin);
        if (distance < min_distance) {
            min_distance = distance;
            closest_char = valid_char;
        }
    }
    return closest_char;
}

// Function to decode the transmission
char decoded_message[2048] = "";
char recovered_stream[2048] = "";
char *decode_transmission(const char *bit_stream, int REP) {
    int bit_stream_length = strlen(bit_stream);

    int i = 0;
    while (i < bit_stream_length) {
        char bit = bit_stream[i];
        int count = 0;
        while (i < bit_stream_length && bit_stream[i] == bit) {
            count++;
            i++;
        }
        int num_bits = round((double)count / REP);
        for (int j = 0; j < num_bits; j++) {
            strncat(recovered_stream, &bit, 1);
        }
    }

    int recovered_length = strlen(recovered_stream);
    for (int i = 0; i < recovered_length; i += 8) {
        if (i + 8 > recovered_length) break;
        char byte_chunk[9] = "";
        strncpy(byte_chunk, recovered_stream + i, 8);
        byte_chunk[8] = '\0';

        // Reverse bits
        char reversed_chunk[9] = "";
        for (int j = 0; j < 8; j++) {
            reversed_chunk[j] = byte_chunk[7 - j];
        }
        reversed_chunk[8] = '\0';

        char decoded_char = (char)strtol(reversed_chunk, NULL, 2);
        if (strchr(VALID_CHARS, decoded_char)) {
            strncat(decoded_message, &decoded_char, 1);
        } else {
            char closest_char = find_closest_valid_char(reversed_chunk);
            strncat(decoded_message, &closest_char, 1);
        }
    }

    return decoded_message;
}

int main() {
    char received_msg[MAX_MSG_SIZE] = { 0 };
    size_t received_msg_size = 0;

    memset(cache_array, 123, L3_SIZE);

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

    // Wait for the init signal. Its a null byte(thrashed cache)
    int zero_bits = 0;
    while (zero_bits < 8) {
        int ones = 0; // probability of one 
        // Count for how many repetions we see bit 1
        for (int j = 0; j < REPEAT; j++) {
            size_t ctr = observe_cache();
            if (ctr > COUNTER_THRESHOLD) {
                ones++;
            }
        }

        if (ones < (REPEAT / 2) + 1) {
            zero_bits++;
        }
    }

    fprintf(stderr, "[RECEIVER] init code received...\n");

    // Array to store received bits. This has a lot of redundancy that makes
    // it too long.
    char bits_original[(MAX_MSG_SIZE * 8 * REPEAT)] = { 0 };
    size_t bits_received = 0;
    char *bits = bits_original;

    unsigned int consecutive_ones = 0;
    // Run until a lot of consecutive 1-bit is received, denoting that the
    // sender has stopped thrashing cache.
    while (consecutive_ones < (10 * REPEAT)) {
        size_t ctr = observe_cache();
        if (ctr > COUNTER_THRESHOLD) {
            bits[bits_received++] = 1;
            consecutive_ones++;
        } else {
            bits[bits_received++] = 0;

            // reset consecutive 1 count
            consecutive_ones = 0;
        }
    }

    fprintf(stderr, "[RECEIVER] message received...\n");

    // convert 'bits' from 0 & 1 to '0' & '1' character 
    for (int i = 0; i < bits_received; i++) {
        bits[i] += '0';
    }

    // Assume that the message is beginning with 'Hi' and the 'bits' array
    // will also have some remenaining bits from the init code(\0) transmitted
    // before that.
    // This means that the bits array will begin with something like this.
    //
    // 000...0000.....0000011111111100000000000000111111110000000001...
    // |     |                                                     |
    // |     |                                                     'i' begins
    // |     'H' (00010010 reversed) begins
    // |
    // part of init code
    //
    // In our reconstructed message, we will hardcode H as first byte and before
    // decoding 'bits' we will make sure it points to where 'i' begins.
    // We will also need to take care of the terminating 1 bits.

    // correctly align 'bits'
    // Ignore the terminating 1 bits
    while (consecutive_ones > 0) {
        bits[bits_received--] = 0;
        consecutive_ones--;
    }

    // ignore the initial few bits as needed
    while (bits[0] == '0') {
        bits++;
        bits_received--;
    }

    while (bits[0] == '1') {
        bits++;
        bits_received--;
    }
    
    while (bits[0] == '0') {
        bits++;
        bits_received--;
    }

    while (bits[0] == '1') {
        bits++;
        bits_received--;
    }

    while (bits[0] == '0') {
        bits++;
        bits_received--;
    }

    // now 'bits' is pointing at 'i' beginning, and the length has also been
    // adjusted

    // decode the bits stream
    char *received_msg_without_H = decode_transmission(bits, REPEAT);
    size_t received_msg_size_without_H = strlen(received_msg_without_H);

    memcpy(received_msg + 1, received_msg_without_H,
            received_msg_size_without_H);

    // put the assumed 'H'
    received_msg[0] = 'H';
    received_msg_size = strlen(received_msg);

    fprintf(stderr, "[RECEIVER] received msg: %s\n", received_msg);

    // DO NOT MODIFY THIS LINE
    printf("Accuracy (%%): %f\n", check_accuracy(received_msg, received_msg_size)*100);
}
