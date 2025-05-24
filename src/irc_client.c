// irc_client.c - Minimal IRC client implementation
#include "irc_client.h"
#include "shared_mem.h"
#include "narrative.h"
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
#include <stdbool.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// Declare these as extern, definition should be in main.c
extern volatile sig_atomic_t terminate_flag;
extern void handle_termination(int sig);

// Helper to extract nick from IRC prefix
static void extract_nick(const char *prefix, char *out, size_t outlen) {
    if (!prefix || prefix[0] != ':') { out[0] = 0; return; }
    const char *bang = strchr(prefix, '!');
    size_t len = bang ? (size_t)(bang - prefix - 1) : strlen(prefix+1);
    if (len >= outlen) len = outlen-1;
    strncpy(out, prefix+1, len);
    out[len] = 0;
}

// Returns 1 if nick is in admin list, 0 otherwise
int is_admin(const BotConfig *config, const char *nick) {
    for (int i = 0; i < config->admin_count; ++i) {
        if (strcasecmp(config->admins[i].name, nick) == 0) {
            return 1;
        }
    }
    return 0;
}

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
    fflush(stdout);
    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", config->channels[channel_index]);
    send(sockfd, buffer, strlen(buffer), 0);
    usleep(100000); // 100ms delay to avoid flooding
    // Main loop: print server messages and listen for pipe commands
    fd_set fds;
    int maxfd = pipe_fd;
    while (!terminate_flag) {
        FD_ZERO(&fds);
        FD_SET(pipe_fd, &fds);
        int ready = select(maxfd+1, &fds, NULL, NULL, NULL);
        if (ready < 0) break;
        if (FD_ISSET(pipe_fd, &fds)) {
            // Read IRC message from main process and respond if needed
            int n = read(pipe_fd, buffer, sizeof(buffer)-1);
            if (n > 0) {
                buffer[n] = 0;
                // Debug: print what the child receives from the pipe
                printf("[CHILD %d] Received from pipe: %s\n", channel_index, buffer);
                fflush(stdout);
                // Process each IRC line in buffer (split on \r\n)
                char *line = buffer;
                while (line && *line) {
                    char *next = strstr(line, "\r\n");
                    if (next) { *next = 0; next += 2; }
                    char *privmsg = strstr(line, "PRIVMSG ");
                    if (privmsg) {
                        // Extract channel/target
                        char *target = privmsg + 8;
                        char *space = strchr(target, ' ');
                        if (!space) { line = next; continue; }
                        *space = 0;
                        // Extract message (after first ' :')
                        char *msg = strstr(space+1, ":");
                        if (!msg) { line = next; continue; }
                        msg++;
                        // Extract sender nick
                        char sender[64] = "";
                        extract_nick(line, sender, sizeof(sender));
                        // Normalize both target and config channel to lowercase for comparison
                        char target_lc[256], config_chan_lc[256];
                        snprintf(target_lc, sizeof(target_lc), "%s", target);
                        snprintf(config_chan_lc, sizeof(config_chan_lc), "%s", config->channels[channel_index]);
                        for (char *p = target_lc; *p; ++p) *p = tolower(*p);
                        for (char *p = config_chan_lc; *p; ++p) *p = tolower(*p);
                        // Admin channel: handle secret commands
                        if (strcmp(config_chan_lc, "#admin") == 0) {
                            // Only accept commands from authenticated admin (handled in main process)
                            if (strncmp(msg, "!stop", 5) == 0) {
                                admin_state->stop_talking = 1;
                                printf("[ADMIN] Stop talking activated\n");
                            } else if (strncmp(msg, "!start", 6) == 0) {
                                admin_state->stop_talking = 0;
                                printf("[ADMIN] Stop talking deactivated\n");
                            } else if (strncmp(msg, "!ignore ", 8) == 0) {
                                strncpy(admin_state->ignored_nick, msg+8, sizeof(admin_state->ignored_nick)-1);
                                admin_state->ignored_nick[sizeof(admin_state->ignored_nick)-1] = 0;
                                printf("[ADMIN] Now ignoring: %s\n", admin_state->ignored_nick);
                            } else if (strncmp(msg, "!topic ", 7) == 0) {
                                strncpy(admin_state->current_topic, msg+7, sizeof(admin_state->current_topic)-1);
                                admin_state->current_topic[sizeof(admin_state->current_topic)-1] = 0;
                                printf("[ADMIN] Topic changed to: %s\n", admin_state->current_topic);
                            }
                        }
                        // For all channels: obey admin state
                        if (strcmp(target_lc, config_chan_lc) == 0) {
                            // If stop_talking is set, do not reply
                            if (admin_state->stop_talking) { line = next; continue; }
                            // If sender is ignored, do not reply
                            if (admin_state->ignored_nick[0] && strcasecmp(sender, admin_state->ignored_nick) == 0) { line = next; continue; }
                            // If topic is set, respond to !topic? with the topic
                            if (strncmp(msg, "!topic?", 7) == 0 && admin_state->current_topic[0]) {
                                char reply[512];
                                snprintf(reply, sizeof(reply), "PRIVMSG %s :Current topic: %s\r\n", target, admin_state->current_topic);
                                printf("[CHILD %d] Sending to IRC: %s\n", channel_index, reply);
                                fflush(stdout);
                                sem_lock();
                                send(sockfd, reply, strlen(reply), 0);
                                sem_unlock();
                                usleep(200000);
                                line = next; continue;
                            }
                            // Normal narrative response
                            const char* reply_text = get_narrative_response(config_chan_lc, msg);
                            if (reply_text) {
                                char reply[512];
                                snprintf(reply, sizeof(reply), "PRIVMSG %s :%s\r\n", target, reply_text);
                                printf("[CHILD %d] Sending to IRC: %s\n", channel_index, reply);
                                fflush(stdout);
                                sem_lock();
                                send(sockfd, reply, strlen(reply), 0);
                                sem_unlock();
                                usleep(200000);
                            }
                        }
                    }
                    line = next;
                }
            }
        }
    }
    // Graceful logoff on termination
    snprintf(buffer, sizeof(buffer), "QUIT :Bot logging off\r\n");
    sem_lock();
    send(sockfd, buffer, strlen(buffer), 0);
    sem_unlock();
    // Do not close sockfd here; main process is responsible for closing the IRC socket
}
