#include <stdio.h>

#define MSG_FILE "msg.txt"
#define MAX_MSG_SIZE 5000

double check_accuracy(char*, int);

// #define SIG_DURATION 512 // 8.15 Mil
// #define SIG_DURATION 1000 // 16 Mil
// #define SIG_DURATION 125 // 1.85 Mil

// Time(ms) for which keep the cache thrashed. Thrashed cache will be 
// interpreted as 0 in the receiver. The number in comment represents the
// counter threshold.
// #define SIG_DURATION 64 // 0.910 Mil
// #define SIG_DURATION 16 // 245000
// #define SIG_DURATION 10 // 150000
#define SIG_DURATION 8 // 125000
// #define SIG_DURATION 4 // 65000
// #define SIG_DURATION 64 // 480000

// Redundancy. Repeat the signal for this many times
#define REPEAT 8
