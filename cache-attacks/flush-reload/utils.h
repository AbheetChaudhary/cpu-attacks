#include <stdio.h>

// update message file name if needed.
#define MSG_FILE "msg.txt"
#define MAX_MSG_SIZE 5000

double check_accuracy(char*, int);

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

// Make sure this file exists.
#define LIBM_PATH "/usr/lib/libm.so.6"

#define REPEAT_COUNT 3 // redundancy in transmitting a bit.

void* map_shared_library(const char*);
void* lookup_function(void*, const char*);
unsigned long current_time_in_ms();
