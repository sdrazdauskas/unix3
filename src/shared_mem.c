// shared_mem.c - Shared memory and semaphore implementation
#include "shared_mem.h"
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <string.h>
#include <unistd.h>

#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define SHM_SIZE 4096

static int shm_id = -1;
static int sem_id = -1;
static char *shared_log = NULL;

// Semaphore helper
static void sem_lock(int semid) {
    struct sembuf sb = {0, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_lock failed");
    }
}
static void sem_unlock(int semid) {
    struct sembuf sb = {0, 1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_unlock failed");
    }
}

int init_shared_resources() {
    shm_id = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id < 0) return -1;
    shared_log = (char*)shmat(shm_id, NULL, 0);
    if (shared_log == (char*)-1) return -1;
    memset(shared_log, 0, SHM_SIZE);
    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (sem_id < 0) return -1;
    semctl(sem_id, 0, SETVAL, 1);
    return 0;
}

void log_message(const char *msg) {
    if (!shared_log) return;
    sem_lock(sem_id);
    strncat(shared_log, msg, SHM_SIZE - strlen(shared_log) - 2);
    strncat(shared_log, "\n", SHM_SIZE - strlen(shared_log) - 1);
    sem_unlock(sem_id);
}

// Helper to safely read the shared log (for admin/debug)
int read_shared_log(char *out, size_t out_size) {
    if (!shared_log) return -1;
    sem_lock(sem_id);
    strncpy(out, shared_log, out_size - 1);
    out[out_size - 1] = 0;
    sem_unlock(sem_id);
    return 0;
}

void cleanup_shared_resources() {
    if (shared_log) shmdt(shared_log);
    if (shm_id >= 0) shmctl(shm_id, IPC_RMID, NULL);
    if (sem_id >= 0) semctl(sem_id, 0, IPC_RMID);
}
