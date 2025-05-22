// main.c - Entry point, process management
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "config.h"
#include "irc_client.h"
#include "narrative.h"
#include "admin.h"
#include "shared_mem.h"

int main(int argc, char *argv[]) {
    // Load configuration
    BotConfig config;
    if (load_config("config/bot.conf", &config) != 0) {
        fprintf(stderr, "Failed to load config\n");
        return 1;
    }

    // Load narratives
    if (load_narratives(config.narratives_path) != 0) {
        fprintf(stderr, "Failed to load narratives\n");
        return 1;
    }

    // Setup shared memory, semaphores, etc.
    if (init_shared_resources() != 0) {
        fprintf(stderr, "Failed to initialize shared resources\n");
        return 1;
    }

    // Fork a process for each channel
    for (int i = 0; i < config.channel_count; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            // Child: handle IRC for this channel
            irc_channel_loop(&config, i);
            exit(0);
        }
    }

    // Main process: wait for children
    while (wait(NULL) > 0);

    cleanup_shared_resources();
    return 0;
}
