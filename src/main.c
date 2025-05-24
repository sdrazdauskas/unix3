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

    // Main process: dispatcher loop
    while (!terminate_flag) {
        // Read from IRC socket
        int n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if (n <= 0) break;
        buffer[n] = 0;
        // Print all server messages for debug
        printf("[IRC] %s", buffer);
        fflush(stdout);
        // Respond to PING
        if (strncmp(buffer, "PING", 4) == 0) {
            char pong[512];
            snprintf(pong, sizeof(pong), "PONG%s\r\n", buffer+4);
            send_irc_message(sockfd, pong);
            printf("[MAIN] PONG\n");
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
                        fflush(stdout);
                        line = next;
                        continue;
                    }
                }
                // Ignore messages from self
                if (strcasecmp(sender, config.nickname) == 0) {
                    printf("[MAIN] Ignoring self message from: %s\n", sender);
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
                // Debug: print what we're comparing for private message auth
                printf("[DEBUG] target_lc='%s', config.nickname='%s'\n", target_lc, config.nickname);
                // --- Admin authentication logic ---
                // If private message to bot, check for !auth
                if (strcasecmp(target_lc, config.nickname) == 0 && strncmp(msg, "!auth ", 6) == 0) {
                    printf("[DEBUG] AUTH attempt: sender='%s', password='%s'\n", sender, msg+6);
                    int found = 0;
                    for (int i = 0; i < config.admin_count; ++i) {
                        printf("[DEBUG] Comparing to admin: name='%s', password='%s'\n", config.admins[i].name, config.admins[i].password);
                        if (strcasecmp(config.admins[i].name, sender) == 0 &&
                            strcmp(config.admins[i].password, msg+6) == 0) {
                            add_authed_admin(sender);
                            found = 1;
                            break;
                        }
                    }
                    if (found) {
                        printf("[AUTH] %s authenticated as admin.\n", sender);
                        // Send a private message to the user
                        char privmsg[256];
                        snprintf(privmsg, sizeof(privmsg), "PRIVMSG %s :Authenticated as admin.\r\n", sender);
                        printf("[DEBUG] full PRIVMSG to user: %s", privmsg); // Show the full message
                        send_irc_message(sockfd, privmsg);
                        // Also send a debug/auth message to #admin channel
                        char adminmsg[256];
                        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :[DEBUG] Authenticated admin: %s\r\n", sender);
                        send_irc_message(sockfd, adminmsg);
                    } else {
                        // Optionally, you could send an auth failed message to #admin as well
                        char failmsg[256];
                        snprintf(failmsg, sizeof(failmsg), "PRIVMSG #admin :[DEBUG] Failed admin auth attempt by: %s\r\n", sender);
                        send_irc_message(sockfd, failmsg);
                    }
                    fflush(stdout);
                    usleep(200000);
                    line = next; continue;
                }
                // Forward all other PRIVMSGs to the correct child (no admin filtering here)
                for (int i = 0; i < config.channel_count; ++i) {
                    snprintf(chan_lc, sizeof(chan_lc), "%s", config.channels[i]);
                    for (char *p = chan_lc; *p; ++p) *p = tolower(*p);
                    if (strcmp(target_lc, chan_lc) == 0) {
                        // Forward the full IRC line to the child, ensure \r\n ending
                        write(pipes[i][1], line, strlen(line));
                        write(pipes[i][1], "\r\n", 2);
                        break;
                    }
                }
            }
            line = next;
        }
    }
    // On termination, signal all children to stop
    for (int i = 0; i < config.channel_count; ++i) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }
    cleanup_shared_resources();
    return 0;
}
