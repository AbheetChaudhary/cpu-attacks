#include <stdio.h>

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

#define LIBM_PATH "/usr/lib/libm.so.6"

#define SIG_DURATION 3

void* map_shared_library(const char*);
void* lookup_function(void*, const char*);
unsigned long current_time_in_ms();
