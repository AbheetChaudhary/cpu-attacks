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

#include "utils.h"

// DO NOT MODIFY THIS FUNCTION
// Modified MAX_FILE_SIZE <- MAX_MSG_SIZE
double check_accuracy(char* received_msg, int received_msg_size){
    FILE *fp = fopen("red_heart.jpg", "r");
    if(fp == NULL){
        printf("Error opening file\n");
        return 1;
    }

    char original_msg[MAX_FILE_SIZE];
    int original_msg_size = 0;
    char c;
    while((c = fgetc(fp)) != EOF){
        original_msg[original_msg_size++] = c;
    }
    fclose(fp);

    int min_size = received_msg_size < original_msg_size ? received_msg_size : original_msg_size;

    int error_count = (original_msg_size - min_size) * 8;
    for(int i = 0; i < min_size; i++){
        char xor_result = received_msg[i] ^ original_msg[i];
        for(int j = 0; j < 8; j++){
            if((xor_result >> j) & 1){
                error_count++;
            }
        }
    }

    double foo =  1-(double)error_count / (original_msg_size * 8);
    printf("check_accuracy: %f\n", foo);
    return foo;
}

void* map_shared_library(const char* path) {
    int fd = open(path, O_RDONLY);
    assert(fd != -1 && "Failed to open library");

    off_t size = lseek(fd, 0, SEEK_END);
    assert(size > 0 && "Failed to get file size");

    // Align size to page boundary
    size_t map_size = (size + 0xFFF) & ~0xFFF;

    void* addr = mmap(NULL, map_size, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
    assert(addr != MAP_FAILED && "mmap failed");

    close(fd);
    return addr;
}

// Generic function to load a symbol dynamically
void* lookup_function(void* handle, const char* func_name) {
    void* symbol = dlsym(handle, func_name);
    if (symbol == NULL) {
        fprintf(stderr, "Failed to find symbol: %s\n", func_name);
        char* error = dlerror();
        if (error) {
            fprintf(stderr, "Error: %s\n", error);
        }
    }
    return symbol;
}

unsigned long current_time_in_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

// Get the function pointers from the specified function names.
// Theses functions must take a double and return one aswell. 
// Raj & Simran agreed on this.
void *get_func_ptrs_array(void* handle, char *func_one_name, char *func_two_name) {
    double (*first_func)(double) =
        (double (*)(double))lookup_function(handle, func_one_name);

    double (*second_func)(double) =
        (double (*)(double))lookup_function(handle, func_two_name);

    void *func_ptrs[2] = {
        (void *) first_func,
        (void *) second_func,
    };

    const static void *function_ptrs[2] = { NULL };

    function_ptrs[0] = func_ptrs[0];
    function_ptrs[1] = func_ptrs[1];

    return function_ptrs;
}

