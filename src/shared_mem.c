// shared_mem.c - Stub for shared memory/semaphores
#include "shared_mem.h"
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>
#include <errno.h>

static int sem_id = -1;

int init_shared_resources() {
    // Stub: just print for now
    printf("Initializing shared resources\n");
    key_t key = ftok("/tmp", 'B');
    if (key == -1) { perror("ftok"); return -1; }
    sem_id = semget(key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); return -1; }
    // Initialize to 1 (unlocked)
    semctl(sem_id, 0, SETVAL, 1);
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
    // Stub: just print for now
    printf("Cleaning up shared resources\n");
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
}
