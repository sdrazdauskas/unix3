// shared_mem.c - Stub for shared memory/semaphores
#include "shared_mem.h"
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static int sem_id = -1;

SharedData *shared_data = NULL;

int init_shared_resources() {
    printf("Initializing shared resources\n");
    key_t key = ftok("/tmp", 'B');
    if (key == -1) { perror("ftok"); return -1; }
    sem_id = semget(key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); return -1; }
    // Initialize to 1 (unlocked)
    semctl(sem_id, 0, SETVAL, 1);

    // Allocate shared memory for SharedData (admin only)
    shared_data = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_data == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(shared_data, 0, sizeof(SharedData));
    // Set up ignore list pointers for shared memory
    extern void set_shared_ignore_ptrs(char (*nicks)[64], int *count);
    set_shared_ignore_ptrs(shared_data->ignored_nicks, &shared_data->ignored_count);
    return 0;
}

int sem_lock() {
    struct sembuf op = {0, -1, SEM_UNDO};
    if (semop(sem_id, &op, 1) == -1) { perror("sem_lock failed"); return -1; }
    return 0;
}

int sem_unlock() {
    struct sembuf op = {0, 1, SEM_UNDO};
    if (semop(sem_id, &op, 1) == -1) { perror("sem_unlock failed"); return -1; }
    return 0;
}

void cleanup_shared_resources() {
    printf("Cleaning up shared resources\n");
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
    if (shared_data) munmap(shared_data, sizeof(SharedData));
}

// Helper to get pointer to shared authed admin struct
void *get_shared_admin_auth_ptr() {
    if (!shared_data) return NULL;
    return &shared_data->authed_admins;
}

// Helper to get pointer to shared authed_count
int *get_shared_authed_count_ptr() {
    if (!shared_data) return NULL;
    return &shared_data->authed_count;
}
