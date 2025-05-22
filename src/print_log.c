// print_log.c - Utility to print the shared log from shared memory
#include "shared_mem.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    if (init_shared_resources() != 0) {
        fprintf(stderr, "Failed to attach to shared memory.\n");
        return 1;
    }
    char buf[4096];
    if (read_shared_log(buf, sizeof(buf)) == 0) {
        printf("--- Shared Log ---\n%s\n-------------------\n", buf);
    } else {
        printf("No log available.\n");
    }
    cleanup_shared_resources();
    return 0;
}
