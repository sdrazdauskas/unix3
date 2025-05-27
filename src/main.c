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
#include "utils.h"
#include <ctype.h>

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
    // Set log file path from config
    set_logfile_path(config.logfile);

    // Load narratives
    trim_whitespace(config.narratives_path);
    if (load_narratives(config.narratives_path) != 0) {
        fprintf(stderr, "Failed to load narratives\n");
        return 1;
    }

    // Setup shared memory, semaphores, etc.
    if (init_shared_resources() != 0) {
        fprintf(stderr, "Failed to initialize shared resources\n");
        return 1;
    }
    // After initializing shared resources in main.c:
    set_shared_admin_auth_ptr(&shared_data->authed_admins);

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
    trim_whitespace(config.server);
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
            // In each child process after mapping shared memory:
            set_shared_admin_auth_ptr(&shared_data->authed_admins);
            irc_channel_loop(&config, i, sockfd, pipes[i][0]);
            exit(0);
        } else if (pid > 0) {
            // Parent: close read end
            close(pipes[i][0]);
            child_pids[i] = pid;
        }
    }

    // Log startup
    log_message("[INFO] Bot started and configuration loaded.");

    // Main process: dispatcher loop
    while (!terminate_flag) {
        // Read from IRC socket
        int n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;
        buffer[n] = 0;
        // Print all server messages for debug
        printf("[IRC] %s", buffer);
        log_message("[IRC] %s", buffer); // Log all IRC server messages
        fflush(stdout);
        // Respond to PING
        if (strncmp(buffer, "PING", 4) == 0) {
            char pong[512];
            snprintf(pong, sizeof(pong), "PONG%s\r\n", buffer+4);
            send_irc_message(sockfd, pong);
            printf("[MAIN] %s\n", pong);
            log_message("[MAIN] PONG %s\n", pong);
            continue;
        }
        // Parse PRIVMSG and forward to correct child
        char *line = buffer;
        while (line && *line) {
            char *next = strstr(line, "\r\n");
            if (next) { *next = 0; next += 2; }
            char *privmsg = strstr(line, "PRIVMSG ");
            if (privmsg) {
                // Extract sender nick (from prefix)
                char sender[64] = "";
                if (line[0] == ':') {
                    const char *bang = strchr(line, '!');
                    size_t len = bang ? (size_t)(bang - line - 1) : strlen(line+1);
                    if (len >= sizeof(sender)) len = sizeof(sender)-1;
                    strncpy(sender, line+1, len);
                    sender[len] = 0;
                }
                // Prevent bot-to-bot loops: ignore nicks starting with 'b' and 9 alphanum
                if (strlen(sender) == 9 && sender[0] == 'b') {
                    int botnick = 1;
                    for (int i = 0; i < 9; ++i) {
                        if (!isalnum(sender[i])) { botnick = 0; break; }
                    }
                    if (botnick) {
                        printf("[MAIN] Ignoring bot nick: %s\n", sender);
                        log_message("[MAIN] Ignoring bot nick: %s", sender);
                        fflush(stdout);
                        line = next;
                        continue;
                    }
                }
                // Ignore messages from self
                if (strcasecmp(sender, config.nickname) == 0) {
                    printf("[MAIN] Ignoring self message from: %s\n", sender);
                    log_message("[MAIN] Ignoring self message from: %s", sender);
                    fflush(stdout);
                    line = next;
                    continue;
                }
                char *target = privmsg + 8;
                char *space = strchr(target, ' ');
                if (!space) { line = next; continue; }
                // Use a temporary buffer for the channel name
                char chan_name[256];
                size_t chan_len = space - target;
                if (chan_len >= sizeof(chan_name)) chan_len = sizeof(chan_name) - 1;
                strncpy(chan_name, target, chan_len);
                chan_name[chan_len] = '\0';
                // Only forward if there is a colon (:) after the channel (i.e., a message)
                char *msg_colon = strchr(space+1, ':');
                if (!msg_colon) { line = next; continue; }
                char *msg = msg_colon + 1;
                // Normalize channel name to lowercase for comparison
                char target_lc[256], chan_lc[256];
                snprintf(target_lc, sizeof(target_lc), "%s", chan_name);
                for (char *p = target_lc; *p; ++p) *p = tolower(*p);

                // Handle private messages to the bot, currently just for auth
                if (strcasecmp(target_lc, config.nickname) == 0 && strncmp(msg, "!auth ", 6) == 0) {
                    try_admin_auth(sender, msg+6, &config, sockfd);
                    line = next; continue;
                }
                // Forward all other PRIVMSGs to the correct child
                for (int i = 0; i < config.channel_count; ++i) {
                    snprintf(chan_lc, sizeof(chan_lc), "%s", config.channels[i]);
                    for (char *p = chan_lc; *p; ++p) *p = tolower(*p);
                    if (strcmp(target_lc, chan_lc) == 0) {
                        // Forward the full IRC line to the child, ensure \r\n ending
                        log_message("[FORWARD] Forwarding message from '%s' to channel '%s'", sender, chan_lc);
                        write(pipes[i][1], line, strlen(line));
                        write(pipes[i][1], "\r\n", 2);
                        break;
                    }
                }
            }
            line = next;
        }
        // Parse NAMES reply (353) and forward to correct child
        if (strstr(buffer, " 353 ")) {
            // Example: :irc.server 353 mynick = #chan :user1 user2 user3\r\n
            char *chan_start = strchr(buffer, '#');
            if (chan_start) {
                char chan_name[256];
                int i = 0;
                while (chan_start[i] && chan_start[i] != ' ' && chan_start[i] != '\r' && chan_start[i] != '\n' && i < 255) {
                    chan_name[i] = chan_start[i];
                    i++;
                }
                chan_name[i] = 0;
                // Find which channel index this is
                int chan_idx = -1;
                for (int c = 0; c < config.channel_count; ++c) {
                    if (strcasecmp(chan_name, config.channels[c]) == 0) {
                        chan_idx = c;
                        break;
                    }
                }
                if (chan_idx != -1) {
                    // Forward the full NAMES reply to the correct child
                    write(pipes[chan_idx][1], buffer, strlen(buffer));
                    write(pipes[chan_idx][1], "\r\n", 2);
                }
            }
        }
    }
    // On termination, signal all children to stop
    for (int i = 0; i < config.channel_count; ++i) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }
    // Wait for all children to exit
    for (int i = 0; i < config.channel_count; ++i) {
        if (child_pids[i] > 0) {
            waitpid(child_pids[i], NULL, 0);
        }
    }
    log_message("[INFO] Bot shutting down.");
    cleanup_shared_resources();
    return 0;
}
