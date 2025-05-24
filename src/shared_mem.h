// shared_mem.h - Shared memory, semaphores, etc.
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

int init_shared_resources();
void cleanup_shared_resources();
int sem_lock();
int sem_unlock();

typedef struct {
    int stop_talking;
    char ignored_nick[64];
    char current_topic[256];
} AdminState;

extern AdminState *admin_state;

#endif
