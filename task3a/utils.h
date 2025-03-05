#include <stdio.h>

#define MSG_FILE "small.png"
#define MAX_FILE_SIZE 512 * 1024 // 512Kb // Do not cross this limit
#define MAX_ADR_SIZE 20  // Do not cross this limit

double check_accuracy(char*, int);

// #define OCCU_SIG_DURATION 512 // 8.15 Mil
// #define OCCU_SIG_DURATION 1000 // 16 Mil
// #define OCCU_SIG_DURATION 125 // 1.85 Mil

// Time(ms) for which keep the cache thrashed. Thrashed cache will be 
// interpreted as 0 in the receiver. The number in comment represents the
// counter threshold for my machine.
// Do not update this unless you are trying to increase bandwidth by
// experimenting.
#define OCCU_SIG_DURATION 8 // 0.910 Mil

// flush+reload signal duration
#define FR_SIG_DURATION 3

// Redundancy. Repeat the signal for this many times
#define REPEAT 5

#define LIBM "libm.so.6" // may be different on user system

void* map_shared_library(const char*);
void* lookup_function(void*, const char*);
unsigned long current_time_in_ms();
void *get_func_ptrs_array(void*, char *, char *);
