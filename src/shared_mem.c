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

AdminState *admin_state = NULL;
SharedData *shared_data = NULL;

#define LOG_BUF_SIZE 4096
#define LOG_FILE_PATH "bot.log"

int init_shared_resources() {
    // Stub: just print for now
    printf("Initializing shared resources\n");
    key_t key = ftok("/tmp", 'B');
    if (key == -1) { perror("ftok"); return -1; }
    sem_id = semget(key, 1, IPC_CREAT | 0666);
    if (sem_id == -1) { perror("semget"); return -1; }
    // Initialize to 1 (unlocked)
    semctl(sem_id, 0, SETVAL, 1);

    // Allocate shared memory for SharedData (admin + log)
    shared_data = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_data == MAP_FAILED) {
        perror("mmap");
        return -1;
    }
    memset(shared_data, 0, sizeof(SharedData));
    admin_state = &shared_data->admin;
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

void log_message(const char *fmt, ...) {
    FILE *f = fopen(LOG_FILE_PATH, "a");
    if (!f) return;
    // Add timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(f, "[%s] ", timebuf);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// Read the shared log into a buffer
int read_shared_log(char *out, int out_size) {
    FILE *f = fopen(LOG_FILE_PATH, "r");
    if (!f) { out[0] = 0; return -1; }
    size_t n = fread(out, 1, out_size-1, f);
    out[n] = 0;
    fclose(f);
    return 0;
}

void cleanup_shared_resources() {
    // Stub: just print for now
    printf("Cleaning up shared resources\n");
    if (sem_id != -1) semctl(sem_id, 0, IPC_RMID);
    if (shared_data) munmap(shared_data, sizeof(SharedData));
}
