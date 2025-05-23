// irc_client.c - Minimal IRC client implementation
#include "irc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>
#include <strings.h> // for strcasecmp
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// Helper: case-insensitive substring search
static int strcasestr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    size_t nlen = strlen(needle);
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0) return 1;
    }
    return 0;
}

// Helper: simple narrative lookup for demonstration
const char* get_narrative_response(const char* channel, const char* msg) {
    // #unix
    if (strcasecmp(channel, "#unix") == 0) {
        if (strcasestr_simple(msg, "ls"))
            return "'ls' lists directory contents. Try 'ls -l' for more details.";
        if (strcasestr_simple(msg, "hello"))
            return "Hello! This is the Unix channel. Ask me anything about Unix!";
        return "I'm here to help with Unix questions.";
    }
    // #random
    if (strcasecmp(channel, "#random") == 0) {
        if (strcasestr_simple(msg, "hello"))
            return "Hey there! Welcome to #random.";
        return "Let's talk about anything!";
    }
    // #admin
    if (strcasecmp(channel, "#admin") == 0) {
        return "Admin channel. Use commands to control the bot.";
    }
    return NULL;
}

// Declare these as extern, definition should be in main.c
extern volatile sig_atomic_t terminate_flag;
extern void handle_termination(int sig);

void irc_channel_loop(const BotConfig *config, int channel_index, int sockfd, int pipe_fd) {
    // Register signal handlers for graceful shutdown
    signal(SIGINT, handle_termination);   // Ctrl+C
    signal(SIGTERM, handle_termination);  // kill
    signal(SIGQUIT, handle_termination);  // Ctrl+'\'
    signal(SIGHUP, handle_termination);   // terminal closed
#ifdef SIGTSTP
    signal(SIGTSTP, handle_termination);  // Ctrl+Z (if available)
#endif
    char buffer[512];
    // Send JOIN for assigned channel (child process only)
    printf("[DEBUG] Child process joining channel: '%s'\n", config->channels[channel_index]);
    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", config->channels[channel_index]);
    send(sockfd, buffer, strlen(buffer), 0);
    // Main loop: print server messages and listen for pipe commands
    fd_set fds;
    int maxfd = (sockfd > pipe_fd) ? sockfd : pipe_fd;
    while (!terminate_flag) {
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        FD_SET(pipe_fd, &fds);
        int ready = select(maxfd+1, &fds, NULL, NULL, NULL);
        if (ready < 0) break;
        if (FD_ISSET(sockfd, &fds)) {
            int n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
            if (n <= 0) break;
            buffer[n] = 0;
            printf("[%s] %s", config->channels[channel_index], buffer);
            // Respond to PING
            if (strncmp(buffer, "PING", 4) == 0) {
                char pong[512];
                snprintf(pong, sizeof(pong), "PONG%s\r\n", buffer+4);
                send(sockfd, pong, strlen(pong), 0);
                continue;
            }
            // Robust PRIVMSG parsing
            char *line = buffer;
            while (line && *line) {
                // Find next line
                char *next = strstr(line, "\r\n");
                if (next) { *next = 0; next += 2; }
                // Check for PRIVMSG
                char *privmsg = strstr(line, "PRIVMSG ");
                if (privmsg) {
                    // Extract sender nick (between ':' and '!')
                    char sender[64] = "";
                    if (line[0] == ':') {
                        char *ex = strchr(line, '!');
                        if (ex && ex - line - 1 < (int)sizeof(sender)) {
                            strncpy(sender, line+1, ex-line-1);
                            sender[ex-line-1] = 0;
                        }
                    }
                    // Forbid bot-to-bot replies: skip if sender matches /^b[a-zA-Z0-9]{8}$/
                    if (strlen(sender) == 9 && sender[0] == 'b') {
                        int botnick = 1;
                        for (int i = 1; i < 9; ++i) {
                            if (!isalnum(sender[i])) { botnick = 0; break; }
                        }
                        if (botnick) { line = next; continue; }
                    }
                    // Extract channel/target
                    char *target = privmsg + 8;
                    char *space = strchr(target, ' ');
                    if (!space) { line = next; continue; }
                    *space = 0;
                    // Extract message (after first ' :')
                    char *msg = strstr(space+1, ":");
                    if (!msg) { line = next; continue; }
                    msg++;
                    // Only respond if target matches our channel (case-insensitive)
                    if (strcasecmp(target, config->channels[channel_index]) == 0) {
                        // DEBUG: print target and config channel in hex only if they match
                        printf("[DEBUG] MATCH: target='%s' (hex:", target);
                        for (size_t i = 0; i < strlen(target); ++i) printf("%02X ", (unsigned char)target[i]);
                        printf(") config='%s' (hex:", config->channels[channel_index]);
                        for (size_t i = 0; i < strlen(config->channels[channel_index]); ++i) printf("%02X ", (unsigned char)config->channels[channel_index][i]);
                        printf(")\n");
                        const char* reply_text = get_narrative_response(target, msg);
                        if (reply_text) {
                            char reply[512];
                            snprintf(reply, sizeof(reply), "PRIVMSG %s :%s\r\n", target, reply_text);
                            send(sockfd, reply, strlen(reply), 0);
                        }
                    }
                }
                line = next;
            }
        }
        if (FD_ISSET(pipe_fd, &fds)) {
            // Read command from main process and send to IRC
            int n = read(pipe_fd, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                send(sockfd, buffer, strlen(buffer), 0);
            }
        }
    }
    // Graceful logoff on termination
    snprintf(buffer, sizeof(buffer), "QUIT :Bot logging off\r\n");
    send(sockfd, buffer, strlen(buffer), 0);
    // Do not close sockfd here; main process is responsible for closing the IRC socket
}
