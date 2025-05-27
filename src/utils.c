#include "utils.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <semaphore.h>

static char logfile_path[256] = "bot.log";
static sem_t log_sem;
static int log_sem_initialized = 0;

void trim_whitespace(char *str) {
    if (!str) return;
    // Trim leading
    char *start = str;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) ++start;
    if (start != str) memmove(str, start, strlen(start) + 1);
    // Trim trailing
    char *end = str + strlen(str) - 1;
    while (end >= str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = 0;
        --end;
    }
}

char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; ++haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            ++h;
            ++n;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL;
}

void set_logfile_path(const char *path) {
    if (path && *path) {
        strncpy(logfile_path, path, sizeof(logfile_path)-1);
        logfile_path[sizeof(logfile_path)-1] = '\0';
    }
}

void log_message(const char *fmt, ...) {
    if (!log_sem_initialized) {
        sem_init(&log_sem, 1, 1);
        log_sem_initialized = 1;
    }
    sem_wait(&log_sem);
    FILE *f = fopen(logfile_path, "a");
    if (!f) {
        sem_post(&log_sem);
        return;
    }
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
    sem_post(&log_sem);
}
