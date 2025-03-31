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

#define SIGNAL_DURATION_TX 1
#define INNER_LOOP_COUNT 1024

#define MSIZE 2048  // Large enough to exceed L3 cache

pid_t child_pid;
timer_t timerid;

char A[MSIZE][MSIZE];
char B[MSIZE][MSIZE];
char C[MSIZE][MSIZE];

void thrasher_matrix_fill(char matrix[MSIZE][MSIZE]) {
    for (size_t i = 0; i < MSIZE; i++)
        for (size_t j = 0; j < MSIZE; j++)
            matrix[i][j] = rand() % 100;
}

void thrasher_matrix_multiply() {
    while (true) {
        // This loops in an intentionally bad order to thrash the cache
        for (size_t i = 0; i < MSIZE; i++) {
            for (size_t k = 0; k < MSIZE; k++) {
                for (size_t j = 0; j < MSIZE; j++) {
                    /* C[i][j] += A[i][k] * B[j][k]; */
                    /* temp += C[i][j]; */
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

void send_bit_one() {
    // Just wait for SIG_DURATION ms

    int ms = SIG_DURATION;
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    int sleep_result = clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
    assert(sleep_result == 0 && "could not completely transmit bit 1");
}

void send_bit_zero() {
    // Launch a child process then create a timer then sleep for SIG_DURATION.
    // After wakeup, immediately kill the child process and exit.

    int ms = SIG_DURATION;
    struct itimerspec tm_spec;
    tm_spec.it_value.tv_sec = ms / 1000;
    tm_spec.it_value.tv_nsec = (ms % 1000) * 1000000;
    tm_spec.it_interval.tv_sec = 0;
    tm_spec.it_interval.tv_nsec = 0;

    child_pid = fork();
    if (child_pid < 0) {
        fprintf(stderr, "[SENDER-ERROR] fork failed.\n");
        exit(-1);
    } else if (child_pid == 0) { // inside child
        // start thrashing the cache
        thrasher_matrix_multiply();
    }

    // Inside parent processor
    
    // arm timer
    timer_settime(timerid, 0, &tm_spec, NULL);

    pause(); // parent process will wait until SIGALRM, then 
            // activate_instant_kill will immediate kill the thrashing
            // child process
}

// send a byte, starting with the least significant bit first.
void send_byte(uint8_t byte) {
    size_t bit_idx = 0;

    while (bit_idx++ < 8) {
        int ls_bit = 0x01 & byte; // least significant bit
        byte /= 2;
        if (ls_bit == 1) {
            for (int rep = 0; rep < REPEAT; rep++) {
                send_bit_one();
            }
        } else {
            for (int rep = 0; rep < REPEAT; rep++) {
                send_bit_zero();
            }
        }
    }
}

void init_transmission() {
    // Thrash the cache.
    // Before this function was called the receiver was having good cache perf.
    // When this function is running the receiver will notice that and exit
    // its init loop.

    send_byte(0);
}

#define end_transmission init_transmission // just send all 1 bits

int main() {
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


    fprintf(stderr,"[SENDER] Run the receiver executable in another terminal and \
put it in listening mode. Then back in this terminal press any key \
to start transmission.");


    int c;
    if ((c = getchar()) == EOF) {
        fprintf(stderr, "[ERROR] keyboard interrupt received. Exiting.\n");
    }

    for (int i = 0; i < UP_DOWNS; i++) {
        if (i % 2 == 0) {
            /* printf("thrashing...%2d\n", i); */
            send_bit_one();
            send_bit_one();
            send_bit_one();
            send_bit_one();
            send_bit_one();
            send_bit_one();
            send_bit_one();
            send_bit_one();
        } else {
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
            send_bit_zero();
        }
    }
}
