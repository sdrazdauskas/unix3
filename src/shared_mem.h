// shared_mem.h - Shared memory, semaphores, etc.
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#define MAX_IGNORED 32
#define MAX_CHANNELS 10

int init_shared_resources();
void cleanup_shared_resources();
int sem_lock();
int sem_unlock();

typedef struct {
    int stop_talking[MAX_CHANNELS];
    char current_topic[256];
    char authed_admins[10][64];
    int authed_count;
    char ignored_nicks[MAX_IGNORED][64];
    int ignored_count;
} SharedData;

extern SharedData *shared_data;

// Helper to get pointer to shared authed admin struct
void *get_shared_admin_auth_ptr();
int *get_shared_authed_count_ptr();

#endif
