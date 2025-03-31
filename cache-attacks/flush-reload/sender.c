#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "utils.h"

#include "cacheutils.h"
#include "utils.h"

// time in ms for which a cache state is to be maintained to register a read.
#define SIGNAL_DURATION_TX 1

#define INNER_LOOP_COUNT 1024

void send_garbage_signal(void *my_sin, void *my_sqrt) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            flush(my_sin);
            flush(my_sqrt);
        }
    }
}

// Beginnig and ending of the communication are handled by this function.
// It caches both pointers so that the receiver will see good cache perf.
// Once the receiver sees that both pointers are cached then it also exits 
// loop to waiting init signal.
void send_init_signal(void *my_sin, void *my_sqrt) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(my_sin);
            maccess(my_sqrt);
        }
    }
    usleep(10);
    flush(my_sin);
    flush(my_sqrt);
}

#define send_end_signal send_init_signal // just send all 1 bits

// sin up, sqrt down
void send_one_signal(void *my_sin, void* my_sqrt) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(my_sin); // my_sin
        }
    }
    usleep(10);
    flush(my_sin);
    flush(my_sqrt);
}

// sin down, sqrt up
void send_zero_signal(void *my_sin, void *my_sqrt) {
    clock_t start_time = current_time_in_ms();
    while ((current_time_in_ms() - start_time) < SIGNAL_DURATION_TX ) {
        for (int inner_loop = 0; inner_loop < INNER_LOOP_COUNT; inner_loop++) {
            maccess(my_sqrt); // my_sqrt
        }
    }
    usleep(10);
    flush(my_sin);
    flush(my_sqrt);
}

// write byte to the channel, starting with the least significant bit first.
void write_byte(unsigned char byte, void *my_sin, void *my_sqrt) {
    size_t bit_idx = 0;

    while (bit_idx++ < 8) {
        int ls_bit = 0x01 & byte; // least significant bit
        byte /= 2;
        if (ls_bit == 1) {
            for (int duration = REPEAT_COUNT; duration > 0; duration--) {
                send_one_signal(my_sin, my_sqrt);
            }
        } else {
            for (int duration = REPEAT_COUNT; duration > 0; duration--) {
                send_zero_signal(my_sin, my_sqrt);
            }
        }
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

int main() {
    FILE *fp = fopen(MSG_FILE, "r");
    if(fp == NULL){
        printf("Error opening file\n");
        return 1;
    }

    char msg[MAX_MSG_SIZE];
    int msg_size = 0;
    char c;
    while((c = fgetc(fp)) != EOF){
        msg[msg_size++] = c;
    }
    fclose(fp);

    clock_t start = clock();

    // Load the libm library symbols via dlopen
    void *handle = dlopen(LIBM_PATH, RTLD_NOW | RTLD_GLOBAL);
    assert(handle && "dlopen failed");

    void **function_ptrs = get_func_ptrs_array(handle);

    // recasted function pointers to void *
    void *my_sin = function_ptrs[0]; // libm's sin()
    void *my_sqrt = function_ptrs[1]; // libm's sqrt()

    // flush all the function pointers
    flush(my_sin);
    flush(my_sqrt);

    fprintf(stderr, "[SENDER] Once the 'receiver' binary is in listening \
mode press any key to start transmitting via covert channel.\n");

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[SENDER] Received EOF key. Exiting.\n");
        exit(-1);
    }

    fprintf(stderr, "[SENDER] Transmitting...");

    // Send init signal to initiate further communication.
    send_init_signal(my_sin, my_sqrt);

    fprintf(stderr, "[SENDER] If the receiver did not acknowledged then please \
restart the whole process again. Make sure to turn off other \
programs\n");

    for (int i = 0; i < msg_size; i++) {
        unsigned char ch = msg[i];
        write_byte(ch, my_sin, my_sqrt);
    }

    // Send end-message signal.
    for (int duration = REPEAT_COUNT; duration > 0; duration--) {
        send_init_signal(my_sin, my_sqrt);
    }

    fprintf(stderr, "[SENDER] The message has been shared.\n");

    // Close the handle to the dl library
    int rv = dlclose(handle);
    if (rv) {
        puts("dlclose error");
        puts(dlerror());
        exit(-1);
    }

    clock_t end = clock();
    double time_taken = ((double)end - start) / CLOCKS_PER_SEC;
    printf("Message sent successfully\n");
    printf("Time taken to send the message: %f\n", time_taken);
    printf("Message size: %d\n", msg_size);
    printf("Bits per second: %f\n", msg_size * 8 / time_taken);

    return 0;
}
