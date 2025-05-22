// shared_mem.h - Shared memory, semaphores, etc.
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <stddef.h>

int init_shared_resources();
void cleanup_shared_resources();
void log_message(const char *msg);
int read_shared_log(char *out, size_t out_size);

#endif
