#include <stdio.h>

#define MSG_FILE "msg.txt"
#define MAX_MSG_SIZE 500

double check_accuracy(char*, int);

// #define SIG_DURATION 512 // 8.15 Mil
// #define SIG_DURATION 1000 // 16 Mil
// #define SIG_DURATION 125 // 1.85 Mil

// Time(ms) for which keep the cache thrashed. Thrashed cache will be 
// interpreted as 0 in the receiver. The number in comment represents the
// counter threshold.
#define SIG_DURATION 2 // 0.910 Mil
// #define SIG_DURATION 32 // 0.910 Mil

// Redundancy. Repeat the signal for this many times
#define REPEAT 6
