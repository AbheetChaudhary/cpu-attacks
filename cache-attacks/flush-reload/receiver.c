#include <stdio.h>

#include "cacheutils.h"
#include "utils.h"

#define HIT_THRESHOLD 95 // Update it as per the result of running 
                         // calibration executable

// time in ms for which a cache state is to be maintained to register a read.
#define SIGNAL_DURATION_RX 1

// enum type of different states the channel can be in
typedef enum {
    ch_one,
    ch_zero,
    ch_init, // init or exit state,
    ch_garbage
} CState; // channel state

void channel_print_state(CState ch_state) {
    if (ch_state == ch_one) {
        printf("channel state: one\n");
    } else if (ch_state == ch_zero) {
        printf("channel state: zero\n");
    } else if (ch_state == ch_init) {
        printf("channel state: init\n");
    } else {
        /* printf("channel state: garbage\n"); */
    }
}

// Observes the channel for SIGNAL_DURATION_RX ms and reports the state
CState channel_state(void *my_sin, void *my_sqrt) {
    bool sine_status = false;
    bool sqrt_status = false;

    size_t one = 0;
    size_t zero = 0;
    size_t init = 0;
    size_t garbage = 0;

    size_t iter = 0;
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_RX) {
        // flush all the function pointers
        flush(my_sin);
        flush(my_sqrt);

        // measure access time for all the function ptrs
        size_t begin = rdtsc();
        maccess(my_sin);
        size_t sine_tics = rdtsc() - begin;

        begin = rdtsc();
        maccess(my_sqrt);
        size_t sqrt_tics = rdtsc() - begin;

        // if all are less than the threshold then init signal received,
        // exit the loop
        if (sine_tics < HIT_THRESHOLD) {
            sine_status = true;
        }
        if (sqrt_tics < HIT_THRESHOLD) {
            sqrt_status = true;
        }

        if (sine_status && !sqrt_status) {
            one++;
        } else if (!sine_status && sqrt_status) {
            zero++;
        } else if (sine_status && sqrt_status) {
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

// Creates an array of function pointers from libm, using the dl api.
// $ man 2 dlsym
void *get_func_ptrs_array(void* handle) {
    double (*my_sin)(double) =
        (double (*)(double))lookup_function(handle, "sin");
    assert((my_sin(3.14159 / 2) - 1.0) < 0.001 &&
           "'sin' received wrong function pointer");

    double (*my_sqrt)(double) =
        (double (*)(double))lookup_function(handle, "sqrt");
    assert((my_sqrt(2.0) - 1.414214) < 0.001 &&
           "'sqrt' received wrong function pointer");

    void *func_ptrs[2] = {
        (void *) my_sin,
        (void *) my_sqrt,
    };

    const static void *function_ptrs[2] = { NULL };

    function_ptrs[0] = func_ptrs[0];
    function_ptrs[1] = func_ptrs[1];

    return function_ptrs;
}

// Takes count as input and returns a state based on that which is maximum
CState max_state(size_t one, size_t zero, size_t init) {
    if (one > zero && one > init) return ch_one;

    if (zero > one && zero > init) return ch_zero;

    if (init > one && init > zero) return ch_init;

    if (one == zero && one < init) return ch_init;
    if (one == zero && one > init) return ch_one; // favour one

    if (one == init && one < zero) return ch_zero;
    if (one == init && one > zero) return ch_init; // favour init

    if (zero == init && zero < one) return ch_one;
    if (zero == init && zero > one) return ch_init; // favour init

    __builtin_unreachable();
}

unsigned char channel_read_byte(void *my_sin, void *my_sqrt) {
    size_t bit_idx = 0;
    unsigned char byte = 0;
    while (bit_idx < 8) {
        // number of times a particular state has been observed
        size_t one_state = 0;
        size_t zero_state = 0;
        size_t init_state = 0; // in case the communication needs to be ended
                               // by spamming init state
        size_t garbage_jumps = 0;
 
        size_t read_threshold = REPEAT_COUNT;
        while (read_threshold-- != 0) {

garbage_detected:
            CState ch_state = channel_state(my_sin, my_sqrt);

            /* channel_print_state(ch_state); */

            if (ch_state == ch_one) {
                one_state++;
            } else if (ch_state == ch_zero) {
                zero_state++;
            } else if (ch_state == ch_init) {
                init_state++;
            } else {
                // if too much garbage then just jump back
                garbage_jumps++;
                if (garbage_jumps < 20000) goto garbage_detected;
            }

            // printf("read_thresh_status: %zu, bit_idx: %zu\n", read_threshold,
                    // bit_idx);
        }

        // decide which state is it and update 'byte' accordingly
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
    char received_msg[MAX_MSG_SIZE] = { 0 };
    int received_msg_size = 0;

    // Load the libm library symbol via dlopen
    void *handle = dlopen(LIBM_PATH, RTLD_NOW | RTLD_GLOBAL);
    assert(handle && "dlopen failed, make sure file at LIBM_PATH exists");

    void **function_ptrs = get_func_ptrs_array(handle);

    // recasted function pointers to void *
    void *my_sin = function_ptrs[0]; // libm's sin()
    void *my_sqrt = function_ptrs[1]; // libm's sqrt()

    // flush all the function pointers
    flush(my_sin);
    flush(my_sqrt);

    fprintf(stderr, "[RECEIVER] Press any key to initiate listening...\n");

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[RECEIVER] Received EOF key. Exiting.\n");
        exit(-1);
    }

    fprintf(stderr, "[RECEIVER] Listening...press enter at sender side\n");

    // This loop will keep running until sender sends the init signal covertly
    // Further communication will happen only after this happens.
    while (true) {
        CState ch_state = channel_state(my_sin, my_sqrt);
        if (ch_state == ch_init) {
            break;
        }
    }
    fprintf(stderr, "[RECEIVER] received init signal...\n");

    // This loop will keep running until the sender emits byte 255(same as init)
    // which denote end of communication.
    while (true) {
        unsigned char ch = channel_read_byte(my_sin, my_sqrt);
        received_msg[received_msg_size++] = ch;
        if (ch == 255) { // init byte was seen again...exit
            received_msg[received_msg_size] = '\0';
            break;
        }
        printf("\rreceived bytes: %d", received_msg_size);
        fflush(stdout);
    }
    printf("\n");
    printf("\rcomplete msg:\n%s\n", received_msg);

    // Close the handle to the library
    int rv = dlclose(handle);
    if (rv) {
        puts("dlclose error");
        puts(dlerror());
        exit(-1);
    }

    // DO NOT MODIFY THIS LINE
    printf("Accuracy (%%): %f\n",
            check_accuracy(received_msg, received_msg_size) * 100);

    return 0;
}
