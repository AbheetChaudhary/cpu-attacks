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

#define MSIZE 2048 // matrix size

pid_t child_pid; // pid of child thread that thrashed cache
timer_t timerid; // timer for the parent thread to precisely control child
                 // thread

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

// Instantly kills the thrashing child process, for precise timing control.
void activate_instant_kill(int signum) {
    if (child_pid > 0) {
        kill(child_pid, SIGKILL);
    }
    // reset child pid
    waitpid(child_pid, NULL, 0);
    child_pid = 0;
    return;
}

// Launch a child process and create a timer then sleep for SIG_DURATION.
// After wakeup, immediately kill the child process and exit.
void send_bit_zero() {

    int ms = SIG_DURATION * REPEAT;
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

    pause(); // parent process will wait for SIGALRM, then 
            // activate_instant_kill will immediate kill the thrashing
            // child process
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

    int _c;
    if ((_c = getchar()) == EOF) {
        fprintf(stderr, "[ERROR] keyboard interrupt received. Exiting.\n");
    }

    // Send init signal by repeatedly sending bit 0, ie thrashing the cache
    // for REPEAT SIG_DURATION intervals
    for (int i = 0; i < REPEAT; i++) {
        send_bit_zero();
    }

    printf("[SENDER] init code transmitted...restart if receiver did not acknowledged \
by logging to terminal\n");

    // transmit each byte
    for (int i = 0; i < msg_size; i++) {
        unsigned char ch = msg[i]; // byte to send

        fprintf(stderr, "sending: '%c'\n", ch);

        // start transmitting this byte, starting from the least significant bit
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            int b = 0x01 & ch;
            ch /= 2;
            if (b == 0) {
                send_bit_zero();
            } else {
                usleep(REPEAT * SIG_DURATION * 1000);
            }
        }
    }

    // Send end signal (null byte)
    for (int i = 0; i < REPEAT; i++) {
        send_bit_zero();
    }

    clock_t end = clock();
    double time_taken = ((double)end - start) / CLOCKS_PER_SEC;
    printf("Message sent successfully\n");
    printf("Time taken to send the message: %f\n", time_taken);
    printf("Message size: %d\n", msg_size);
    printf("Bits per second: %f\n", msg_size * 8 / time_taken);

    return 0;
}
