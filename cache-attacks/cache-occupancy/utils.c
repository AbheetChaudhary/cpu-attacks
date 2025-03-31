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
double check_accuracy(char* received_msg, int received_msg_size){
    FILE *fp = fopen(MSG_FILE, "r");
    if(fp == NULL){
        printf("Error opening file\n");
        return 1;
    }

    char original_msg[MAX_MSG_SIZE];
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

    return 1-(double)error_count / (original_msg_size * 8);
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
