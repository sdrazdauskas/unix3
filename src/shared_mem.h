// shared_mem.h - Shared memory, semaphores, etc.
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

int init_shared_resources();
void cleanup_shared_resources();
int sem_lock();
int sem_unlock();

#endif
