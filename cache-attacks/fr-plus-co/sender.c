#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sched.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>

#include <sys/wait.h>
#include <signal.h>

#include "cacheutils.h"
#include "utils.h"

// Update the library to try something different.
// LIB_NAME is the short name of the library as used by gcc.
// Ex: ssl for libssl, crypto for libcryoto, c for libc, etc.
#define LIB_NAME "c" // 'm' is from '-lm', 'c' is from '-lc' for libm & libc in gcc

// This full name is what the receiver will expand to for the above LIB_NAME
// example. Receiver will first expand it to the name as used by gcc then add a
// suffix to create the full library name. The ending .so.6 value is hardcoded
// on receiver side. Make sure that whatever library name you use, its
// corresponding full name exists somewhere in the standard library directories
#define FULL_LIB_NAME "libc.so.6" // make sure the full lib path exists for
                                  // whatever library you use

// These functions must be in the LIB_NAME library
// Make sure these functions are independent and dont rely on each other for
// calculations. 
// Make sure these function take a double and return a double. This 
// particular function signature is hardcoded in the rest of the code.
//
// CAUTION: Do not use common functions like malloc or free, they are always being used
// by something in the background and that will cause false positives.
#define FIRST_FUNC_NAME "ffs"
#define SECOND_FUNC_NAME "bzero"

// Contact info as per the protocol.
#define CONTACT_INFO ("Hi" LIB_NAME ":" FIRST_FUNC_NAME ":" SECOND_FUNC_NAME)

#define HIT_THRESHOLD 95 // from flush+reload

#define SIGNAL_DURATION_RX 1 // from flush+reload

#define MSIZE 2048 // matrix size

pid_t child_pid;
timer_t timerid;

char A[MSIZE][MSIZE];
char B[MSIZE][MSIZE];
char C[MSIZE][MSIZE];

// Initialize matrix
void thrasher_matrix_fill(char matrix[MSIZE][MSIZE]) {
    for (size_t i = 0; i < MSIZE; i++)
        for (size_t j = 0; j < MSIZE; j++)
            matrix[i][j] = rand() % 100;
}

// Thrashes the l3 cache. This function never returns, it only just gets 
// killed by its parent once a fixed amount of time is passed.
void thrasher_matrix_multiply() {
    while (true) {
        // This loops in an intentionally bad order to thrash the cache
        for (size_t i = 0; i < MSIZE; i++) {
            for (size_t k = 0; k < MSIZE; k++) {
                for (size_t j = 0; j < MSIZE; j++) {
                    maccess(&A[i][k]);
                    maccess(&B[i][k]);
                }
            }
        }
    }
}

// Instantly kills the thrashing child process.
void activate_instant_kill(int signum) {
    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
    }
    // reset child pid
    waitpid(child_pid, NULL, 0);
    child_pid = 0;
    return;
}

// To send the bit 1, just sleep for the signal duration period
void send_bit_one() {
    // Just wait for OCCU_SIG_DURATION ms

    int ms = OCCU_SIG_DURATION;
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    int sleep_result = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    assert(sleep_result == 0 && "could not completely transmit bit 1");
}

// Launch a child process then create a timer then sleep for OCCU_SIG_DURATION.
// After wakeup, immediately kill the child process and exit.
void send_bit_zero() {

    int ms = OCCU_SIG_DURATION * REPEAT;
    struct itimerspec tm_spec;
    tm_spec.it_value.tv_sec = ms / 1000;
    tm_spec.it_value.tv_nsec = (ms % 1000) * 1000000;
    tm_spec.it_interval.tv_sec = 0;
    tm_spec.it_interval.tv_nsec = 0;

    child_pid = fork();
    if (child_pid < 0) {
        fprintf(stderr, "[SIMRAN-ERROR] fork failed.\n");
        exit(-1);
    } else if (child_pid == 0) { // inside child
        // start thrashing the cache
        thrasher_matrix_multiply();
    }

    // Inside parent processor
    
    // arm timer
    timer_settime(timerid, 0, &tm_spec, NULL);

    pause(); // parent process will wait for SIGALRM, then 
            // activate_instant_kill will immediate kill the thrashing
            // child process
}

// enum type of different states the channel can be in
typedef enum {
    ch_one,
    ch_zero,
    ch_init, // init or exit state,
    ch_garbage
} CState; // channel state

CState channel_state(void *first_func, void *second_func) {
    bool first_func_status = false;
    bool second_func_status = false;

    size_t one = 0;
    size_t zero = 0;
    size_t init = 0;
    size_t garbage = 0;

    size_t iter = 0;
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_RX) {
        // flush all the function pointers
        flush(first_func);
        flush(second_func);

        // measure access time for all the function ptrs
        size_t begin = rdtsc();
        maccess(first_func);
        size_t first_func_tics = rdtsc() - begin;

        begin = rdtsc();
        maccess(second_func);
        size_t second_func_tics = rdtsc() - begin;

        // if all are less than the threshold then init signal received,
        // exit the loop
        if (first_func_tics < HIT_THRESHOLD) {
            first_func_status = true;
        }
        if (second_func_tics < HIT_THRESHOLD) {
            second_func_status = true;
        }

        if (first_func_status && !second_func_status) {
            one++;
        } else if (!first_func_status && second_func_status) {
            zero++;
        } else if (first_func_status && second_func_status) {
            init++;
        } else {
            garbage++;
        }

        iter++;
    }

    // printf("iteration: %zu:\tone: %zu, zero: %zu, init: %zu, garbage: %zu\n", 
            // iter, one, zero, init, garbage);

    size_t max = MAX(MAX(MAX(one, zero), init), garbage);

    if (max == one) {
        return ch_one;
    } else if (max == zero) {
        return ch_zero;
    } else if (max == init) {
        return ch_init;
    } else {
        return ch_garbage;
    }
}

CState max_state(size_t one, size_t zero, size_t init) {
    if (one > zero && one > init) return ch_one;

    if (zero > one && zero > init) return ch_zero;

    if (init > one && init > zero) return ch_init;

    if (one == zero && one < init) return ch_init;
    if (one == zero && one > init) return ch_one; // 

    if (one == init && one < zero) return ch_zero;
    if (one == init && one > zero) return ch_init; //

    if (zero == init && zero < one) return ch_one;
    if (zero == init && zero > one) return ch_init; //

    __builtin_unreachable();
}

unsigned char channel_read_byte(void *first_func, void *second_func) {
    size_t bit_idx = 0;
    unsigned char byte = 0;
    while (bit_idx < 8) {
        size_t one_state = 0;
        size_t zero_state = 0;
        size_t init_state = 0; // in case the communication needs to be ended
                               // by spamming init state
        size_t garbage_jumps = 0;
 
        size_t read_threshold = FR_SIG_DURATION;
        while (read_threshold-- != 0) {

garbage_detected:
            CState ch_state = channel_state(first_func, second_func);

            /* channel_print_state(ch_state); */

            if (ch_state == ch_one) {
                one_state++;
            } else if (ch_state == ch_zero) {
                zero_state++;
            } else if (ch_state == ch_init) {
                init_state++;
            } else {
                // if too much garbage then just exit
                garbage_jumps++;
                if (garbage_jumps < 20000) goto garbage_detected;
            }

            // printf("read_thresh_status: %zu, bit_idx: %zu\n", read_threshold,
                    // bit_idx);
        }

        CState max = max_state(one_state, zero_state, init_state);

        if (max == ch_one) {
            byte += (1 << bit_idx);
            bit_idx++;
        } else if (max == ch_init) {
            bit_idx++; // NOP
            return 255; // immediate return. init state is special
        } else {
            bit_idx++;
        }
    }

    return byte;
}

int main() {
    char contact[] = CONTACT_INFO;
    size_t contact_size = sizeof(contact);

    // set signal handlers
    struct sigaction s_zero;
    s_zero.sa_handler = activate_instant_kill;
    sigemptyset(&s_zero.sa_mask);
    s_zero.sa_flags = 0;
    sigaction(SIGALRM, &s_zero, NULL);

    // create timer
    struct sigevent sev;
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;
    sev.sigev_value.sival_ptr = &timerid;

    int timer_create_status = timer_create(CLOCK_MONOTONIC, &sev, &timerid);
    assert(timer_create_status == 0 && "failed to create high resolution timer");

    // fill the matrices
    thrasher_matrix_fill(A);
    thrasher_matrix_fill(B);

    fprintf(stderr,"[SIMRAN] Run the receiver executable in another terminal and \
put it in listening mode. Then back in this terminal press any key \
to start transmission.");

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[ERROR] keyboard interrupt received. Exiting.\n");
    }

    // Send init signal by repeatedly sending bit 0, ie thrashing the cache
    // for REPEAT SIG_DURATION intervals
    for (int i = 0; i < REPEAT; i++) {
        send_bit_zero();
    }

    printf("[SIMRAN] init code transmitted...restart if receiver did not acknowledged \
by logging to terminal\n");

    // transmit each byte
    for (int i = 0; i < contact_size; i++) {
        unsigned char ch = contact[i]; // byte to send

        fprintf(stderr, "sending: '%c'\n", ch);

        // start transmitting this byte, starting from the least significant bit
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            int b = 0x01 & ch;
            ch /= 2;
            if (b == 0) {
                send_bit_zero();
            } else {
                usleep(REPEAT * OCCU_SIG_DURATION * 1000);
            }
        }
    }

    // Send end signal (null byte)
    for (int i = 0; i < REPEAT; i++) {
        send_bit_zero();
    }

    fprintf(stderr, "[SIMRAN] contact info send successfully...i'll sleep \
for a while.\n");

    // Simran sleeps less and wakes up before Raj starts transmitting.
    sleep(8);

    fprintf(stderr, "[SIMRAN] Listening back from Raj. He will soon wakeup.\n");

    char received_msg[MAX_FILE_SIZE] = { 0 };

    // Load the library symbol via dlopen
    void *handle = dlopen(FULL_LIB_NAME, RTLD_NOW | RTLD_GLOBAL);
    assert(handle && "dlopen failed");

    void **function_ptrs = get_func_ptrs_array(handle, 
            FIRST_FUNC_NAME, SECOND_FUNC_NAME);

    // recasted function pointers to void *
    void *first_func = function_ptrs[0];
    void *second_func = function_ptrs[1];

    while (true) {
        CState ch_state = channel_state(first_func, second_func);
        if (ch_state == ch_init) {
            break;
        }
    }

    fprintf(stderr, "[SIMRAN] received init code...\n");

    // receive file size as int
    int file_size = 0;
    for (int size_bit = 0; size_bit < sizeof(int); size_bit++) {
        unsigned char ch = channel_read_byte(first_func, second_func);

        int shifted_byte = ch;
        for (int i = 0; i < size_bit; i++) {
            shifted_byte *= 256;
        }

        file_size += shifted_byte;
    }

    fprintf(stderr, "[SIMRAN] file size received: %d\n", file_size);

    // receive that many bytes now.
    for (int i = 0; i < file_size; i++) {
        unsigned char ch = channel_read_byte(first_func, second_func);
        received_msg[i] = ch;
        /* printf("\rreceived_msg_size: %d", received_msg_size); */
        if (i % 512 == 0) {
            printf("\rreceived: %5d bytes...", i);
            fflush(stdout);
        }
        /* fflush(stdout); */
    }
    printf("\n");
    fprintf(stderr, "[SIMRAN] file received\n");
    /* printf("\rComplete msg: %s\n", received_msg); */
    char *received_filename = "from_raj_to_simran.bin";
    FILE *fp = fopen(received_filename, "w");
    if(fp == NULL){
        printf("Error opening file to write received data\n");
        return 1;
    }
    int write_len = fwrite(received_msg, 1, file_size, fp);
    assert(write_len == file_size && "Some writes failed");

    fprintf(stderr, "[SIMRAN] Successfully written %d bytes of data to %s\n",
            file_size, received_filename);
    fprintf(stderr, "[SIMRAN] manually check the quality by viewing the file...");

    // Close the handle to the library
    int rv = dlclose(handle);
    if (rv) {
        puts("dlclose error");
        puts(dlerror());
        exit(-1);
    }

    return 0;
}
