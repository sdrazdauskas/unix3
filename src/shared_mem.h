// shared_mem.h - Shared memory, semaphores, etc.
#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#define LOG_BUF_SIZE 4096

int init_shared_resources();
void cleanup_shared_resources();
int sem_lock();
int sem_unlock();

typedef struct {
    int stop_talking;
    char ignored_nick[64];
    char current_topic[256];
} AdminState;

typedef struct {
    AdminState admin;
    char log[LOG_BUF_SIZE];
    int log_offset;
} SharedData;

extern AdminState *admin_state;
extern SharedData *shared_data;

void log_message(const char *fmt, ...);
int read_shared_log(char *out, int out_size);

#endif
