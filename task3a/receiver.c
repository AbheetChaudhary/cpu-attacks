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

size_t COUNTER_THRESHOLD = 120000; // update for you machine

#define LIB_NAME_END ".so.6" // update if needed
#define LIB_NAME_BEGIN "lib"

// Update if needed
#define VALID_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz :.,\n'\0"

#define SIGNAL_DURATION_TX 1
#define INNER_LOOP_COUNT 1024

#define L3_SIZE 4 * 1024 * 1024 // 4MB


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

// Convert CPU cycles to microseconds
static inline uint64_t __attribute__ ((always_inline)) rdtsc_to_us(uint64_t start, uint64_t freq) {
    return (start * 1000000ULL) / freq;  // Convert CPU cycles to µs
}

// Converts time(real, not normalized) time from rdtsc to 
static inline uint64_t __attribute__ ((always_inline))
scaled_time(uint64_t time, float scale_factor) {
    return (uint64_t) ((float) time * scale_factor);
}


// Get CPU TSC frequency (run once)
uint64_t get_cpu_freq() {
    struct timespec ts1, ts2;
    uint64_t start, end;

    // Get start timestamp and TSC value
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    start = __rdtsc();

    // Sleep for 100ms
    usleep(100000);

    // Get end timestamp and TSC value
    clock_gettime(CLOCK_MONOTONIC, &ts2);
    end = __rdtsc();

    // Compute elapsed time in nanoseconds
    uint64_t elapsed_ns = (ts2.tv_sec - ts1.tv_sec) * 1000000000ULL
                        + (ts2.tv_nsec - ts1.tv_nsec);

    // Convert to seconds and compute frequency
    return (end - start) * 1000000000ULL / elapsed_ns;
}

// Observe cache. Some care has been taken to run it exactly for
// OCCU_SIG_DURATION milliseconds by inlining important function calls and
// de-normalizing time as measured by rdtsc
size_t observe_cache(void) {
    size_t counter = 0;

    volatile uint8_t temp = 0;
    size_t run_duration = OCCU_SIG_DURATION * 1000; // in microsesc

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

void send_init_signal(void *first_func, void *second_func) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(first_func);
            maccess(second_func);
        }
    }
    usleep(10);
    flush(first_func);
    flush(second_func);
}

#define send_end_signal send_init_signal // just send all 1 bits

void send_one_signal(void *first_func, void* second_func) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(first_func); // first_func
        }
    }
    usleep(10);
    flush(first_func);
    flush(second_func);
}

void send_zero_signal(void *first_func, void *second_func) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(second_func); // second_func
        }
    }
    usleep(10);
    flush(first_func);
    flush(second_func);
}

// write byte to the channel, starting with the least significant bit first.
void write_byte(unsigned char byte, void *first_func, void *second_func) {
    size_t bit_idx = 0;

    while (bit_idx++ < 8) {
        int ls_bit = 0x01 & byte; // least significant bit
        byte /= 2;
        if (ls_bit == 1) {
            for (int duration = FR_SIG_DURATION; duration > 0; duration--) {
                send_one_signal(first_func, second_func);
            }
        } else {
            for (int duration = FR_SIG_DURATION; duration > 0; duration--) {
                send_zero_signal(first_func, second_func);
            }
        }
    }
}

int main() {
    char contact_address[MAX_ADR_SIZE] = { 0 };

    memset(cache_array, 123, L3_SIZE);

    FREQ = get_cpu_freq();  // get current cpu freq
    uint64_t start = __rdtsc();
    usleep(1000);
    uint64_t elapsed = __rdtsc() - start;
    SCALE_FACTOR = (float) rdtsc_to_us(elapsed, FREQ) / 1000;

    fprintf(stderr, "[RAJ] Press any key to initiate listening...\n");

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[RAJ] Received EOF key. Exiting.\n");
        exit(-1);
    }

    fprintf(stderr, "[RAJ] Listening...press enter at sender side\n");

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

    fprintf(stderr, "[RAJ] init code received...\n");

    // Array to store received bits. This has a lot of redundancy that makes
    // it too long.
    char bits_original[(MAX_ADR_SIZE * 8 * REPEAT)] = { 0 };
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

    fprintf(stderr, "[RAJ] message received...\n");

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

    memcpy(contact_address + 1, received_msg_without_H,
            received_msg_size_without_H);

    // put the assumed 'H'
    contact_address[0] = 'H';

    fprintf(stderr, "[RAJ] received address: %s\n", contact_address);
    
    // Everything above this is same as in task2b

    // Parse the address to create library and function names

    // Leave the original, work with copy
    char *contact = contact_address;

    // This will give "Hi..."
    char *first_token = strsep(&contact, ":");

    // Ignore 'Hi'
    first_token += 2;

    // Complete library name will be libXXXX.so.o and NULL byte
    char *library_name = malloc(strlen(LIB_NAME_BEGIN) + strlen(first_token)
            + strlen(LIB_NAME_END) + 1); // free it later
    assert(library_name != NULL && "allocation for library name failed");

    library_name[0] = 0; // make null

    strcat(library_name, LIB_NAME_BEGIN);
    strcat(library_name, first_token);
    strcat(library_name, LIB_NAME_END);

    char *first_func_name = strsep(&contact, ":"); // sine
    char *second_func_name = strsep(&contact, ":"); // sqrt

    fprintf(stderr, "[RAJ] received library: %s\n", library_name);
    fprintf(stderr, "[RAJ] received first func: %s\n", first_func_name);
    fprintf(stderr, "[RAJ] received second func: %s\n", second_func_name);

    // Raj sleeps longer than Simran
    sleep(10);
    fprintf(stderr, "[RAJ] Transmitting back to Simran. She must already be in \
listening state.\n");

    // Raj is sender now
    // Open the MSG_FILE in binary format
    FILE *fp = fopen(MSG_FILE, "rb");
    if(fp == NULL){
        printf("Error opening MSG_FILE file\n");
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    int msg_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *msg = malloc(msg_size + 1);
    assert(msg != NULL && "malloc failed");
    
    fread(msg, 1, msg_size, fp);
    fclose(fp);

    msg[msg_size] = 0;

    // Load the library symbols via dlopen
    void *handle = dlopen(library_name, RTLD_NOW | RTLD_GLOBAL);
    assert(handle && "dlopen failed");

    void **function_ptrs = get_func_ptrs_array(handle, 
            first_func_name, second_func_name);

    // recasted function pointers to void *
    void *first_func = function_ptrs[0];
    void *second_func = function_ptrs[1];

    // flush all the function pointers
    flush(first_func);
    flush(second_func);

    // Send init signal to initiate further communication.
    /* send_init_signal(first_func, second_func); */

    // Send init signal. Just once, not like end signal
    send_init_signal(first_func, second_func);

    fprintf(stderr, "[RAJ] init signal transmitted...\n");
    fprintf(stderr, "[RAJ] If the receiver did not acknowledged then please \
restart the whole process again. Make sure to turn off other \
programs\n");

    // send file size to simran. starting from lsb
    int file_size = msg_size;
    for (int bit_idx = 0; bit_idx < sizeof(int); bit_idx++) {
        char lsb = 0xff & file_size;
        write_byte(lsb, first_func, second_func);
        file_size /= 256;
    }

    // send data to simran
    for (int i = 0; i < msg_size; i++) {
        unsigned char ch = msg[i];
        write_byte(ch, first_func, second_func);

        if (i % 512 == 0) { // log at every 1K bytes
            printf("\rbytes transmitted: %6d/%d", i, msg_size);
            fflush(stdout);
        }
    }
    printf("\n");

    // Send end-message signal.
    for (int duration = FR_SIG_DURATION * 2; duration > 0; duration--) {
        send_end_signal(first_func, second_func);
    }

    fprintf(stderr, "[RAJ] The message has been shared.\n");

    free(library_name);

    // Close the handle to the dl library
    int rv = dlclose(handle);
    if (rv) {
        puts("dlclose error");
        puts(dlerror());
        exit(-1);
    }

    return 0;
}
