// main.c - Entry point, process management
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include "config.h"
#include "irc_client.h"
#include "narrative.h"
#include "admin.h"
#include "shared_mem.h"

volatile sig_atomic_t terminate_flag = 0;

void handle_termination(int sig) {
    terminate_flag = 1;
}

int main(int argc, char *argv[]) {
    // Register signal handlers for graceful shutdown
    signal(SIGINT, handle_termination);   // Ctrl+C
    signal(SIGTERM, handle_termination);  // kill
    signal(SIGQUIT, handle_termination);  // Ctrl+'\'
    signal(SIGHUP, handle_termination);   // terminal closed
#ifdef SIGTSTP
    signal(SIGTSTP, handle_termination);  // Ctrl+Z (if available)
#endif
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

    // Main process: connect to IRC server first
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[512];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        return 1;
    }
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // Remove leading/trailing whitespace from config->server
    char server_addr[256];
    strncpy(server_addr, config.server, sizeof(server_addr)-1);
    server_addr[sizeof(server_addr)-1] = '\0';
    char *start = server_addr;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        --end;
    }
    if (inet_aton(start, &serv_addr.sin_addr)) {
        // Parsed as IP
    } else {
        struct hostent *server = gethostbyname(start);
        if (server == NULL) {
            fprintf(stderr, "ERROR, no such host: %s\n", start);
            return 1;
        }
        memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    }
    serv_addr.sin_port = htons(config.port);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        return 1;
    }
    // IRC handshake (main process only)
    snprintf(buffer, sizeof(buffer), "NICK %s\r\n", config.nickname);
    send(sockfd, buffer, strlen(buffer), 0);
    snprintf(buffer, sizeof(buffer), "USER %s 0 * :%s\r\n", config.nickname, config.nickname);
    send(sockfd, buffer, strlen(buffer), 0);
    // Create pipes for communication with each child
    int pipes[MAX_CHANNELS][2];
    pid_t child_pids[MAX_CHANNELS] = {0};
    for (int i = 0; i < config.channel_count; ++i) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return 1;
        }
        pid_t pid = fork();
        if (pid == 0) {
            // Child: close write end, pass read end to irc_channel_loop
            close(pipes[i][1]);
            irc_channel_loop(&config, i, sockfd, pipes[i][0]);
            exit(0);
        } else if (pid > 0) {
            // Parent: close read end
            close(pipes[i][0]);
            child_pids[i] = pid;
        }
    }
    // Main process: example of sending a message to each child
    // (In real use, this would be based on user/admin input or logic)
    for (int i = 0; i < config.channel_count; ++i) {
        char msg[256];
        snprintf(msg, sizeof(msg), "PRIVMSG %s :Hello from main process!\n", config.channels[i]);
        write(pipes[i][1], msg, strlen(msg));
    }
    // Main process: wait for children or termination signal
    while (!terminate_flag && wait(NULL) > 0);
    // On termination, signal all children to stop
    for (int i = 0; i < config.channel_count; ++i) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }
    cleanup_shared_resources();
    return 0;
}
