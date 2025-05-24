// irc_client.c - Minimal IRC client implementation
#include "irc_client.h"
#include "shared_mem.h"
#include "narrative.h"
#include "admin.h"
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

static char ignored_nicks[MAX_IGNORED][64];
static int ignored_count = 0;

int is_ignored_user(const char *nick) {
    for (int i = 0; i < ignored_count; ++i) {
        if (strcasecmp(ignored_nicks[i], nick) == 0) return 1;
    }
    return 0;
}

void add_ignored_user(const char *nick) {
    if (!is_ignored_user(nick) && ignored_count < MAX_IGNORED) {
        strncpy(ignored_nicks[ignored_count], nick, 63);
        ignored_nicks[ignored_count][63] = 0;
        ignored_count++;
    }
}

void remove_ignored_user(const char *nick) {
    for (int i = 0; i < ignored_count; ++i) {
        if (strcasecmp(ignored_nicks[i], nick) == 0) {
            for (int j = i; j < ignored_count - 1; ++j) {
                strncpy(ignored_nicks[j], ignored_nicks[j+1], 64);
            }
            ignored_count--;
            break;
        }
    }
}

void clear_ignored_users(void) {
    ignored_count = 0;
}

// Helper to send IRC message with locking and delay
void send_irc_message(int sockfd, const char *msg) {
    sem_lock();
    send(sockfd, msg, strlen(msg), 0);
    sem_unlock();
    usleep(100000); // 100ms delay to avoid flooding
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
    send_irc_message(sockfd, buffer);
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
                            if (!is_authed_admin(sender)) {
                                char warnmsg[256];
                                snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You must authenticate with /msg %s !auth password before using admin commands.\r\n", config->nickname);
                                send_irc_message(sockfd, warnmsg);
                                continue;
                            } else {
                                char warnmsg[256];
                                snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You are authenticated. Enter your admin command.\r\n");
                                send_irc_message(sockfd, warnmsg);
                                continue;
                            }
                            if (strncmp(msg, "!stop ", 6) == 0) {
                                // !stop <channel>
                                char *chan = msg + 6;
                                for (int i = 0; i < MAX_CHANNELS; ++i) {
                                    if (strcasecmp(chan, config->channels[i]) == 0) {
                                        admin_state->stop_talking = 1;
                                        printf("[ADMIN] Stop talking activated for channel: %s\n", chan);
                                        char adminmsg[256];
                                        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Bot will stop talking in %s.\r\n", chan);
                                        send_irc_message(sockfd, adminmsg);
                                        break;
                                    }
                                }
                                continue;
                            } else if (strncmp(msg, "!start ", 7) == 0) {
                                // !start <channel>
                                char *chan = msg + 7;
                                for (int i = 0; i < MAX_CHANNELS; ++i) {
                                    if (strcasecmp(chan, config->channels[i]) == 0) {
                                        admin_state->stop_talking = 0;
                                        printf("[ADMIN] Stop talking deactivated for channel: %s\n", chan);
                                        char adminmsg[256];
                                        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Bot will resume talking in %s.\r\n", chan);
                                        send_irc_message(sockfd, adminmsg);
                                        break;
                                    }
                                }
                                continue;
                            } else if (strncmp(msg, "!ignore ", 8) == 0) {
                                add_ignored_user(msg+8);
                                printf("[ADMIN] Now ignoring: %s\n", msg+8);
                                char adminmsg[256];
                                snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Now ignoring user: %s\r\n", msg+8);
                                send_irc_message(sockfd, adminmsg);
                                continue;
                            } else if (strncmp(msg, "!removeignore ", 14) == 0) {
                                remove_ignored_user(msg+14);
                                printf("[ADMIN] Ignore removed for: %s\n", msg+14);
                                char adminmsg[256];
                                snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Ignore removed for user: %s\r\n", msg+14);
                                send_irc_message(sockfd, adminmsg);
                                continue;
                            } else if (strncmp(msg, "!clearignore", 12) == 0) {
                                clear_ignored_users();
                                printf("[ADMIN] All ignores cleared.\n");
                                char adminmsg[256];
                                snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :All ignores cleared.\r\n");
                                send_irc_message(sockfd, adminmsg);
                                continue;
                            } else if (strncmp(msg, "!topic ", 7) == 0) {
                                strncpy(admin_state->current_topic, msg+7, sizeof(admin_state->current_topic)-1);
                                admin_state->current_topic[sizeof(admin_state->current_topic)-1] = 0;
                                printf("[ADMIN] Topic changed to: %s\n", admin_state->current_topic);
                                continue;
                            }
                        }
                        // For all channels: obey admin state
                        if (strcmp(target_lc, config_chan_lc) == 0) {
                            // If stop_talking is set, do not reply
                            if (admin_state->stop_talking) { line = next; continue; }
                            // If sender is ignored, do not reply
                            if (is_ignored_user(sender)) { line = next; continue; }
                            // If topic is set, respond to !topic? with the topic
                            if (strncmp(msg, "!topic?", 7) == 0 && admin_state->current_topic[0]) {
                                char reply[512];
                                snprintf(reply, sizeof(reply), "PRIVMSG %s :Current topic: %s\r\n", target, admin_state->current_topic);
                                printf("[CHILD %d] Sending to IRC: %s\n", channel_index, reply);
                                fflush(stdout);
                                send_irc_message(sockfd, reply);
                                line = next; continue;
                            }
                            // Normal narrative response
                            const char* reply_text = get_narrative_response(config_chan_lc, msg);
                            if (reply_text) {
                                char reply[512];
                                snprintf(reply, sizeof(reply), "PRIVMSG %s :%s\r\n", target, reply_text);
                                printf("[CHILD %d] Sending to IRC: %s\n", channel_index, reply);
                                fflush(stdout);
                                send_irc_message(sockfd, reply);
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
    send_irc_message(sockfd, buffer);
    // Do not close sockfd here; main process is responsible for closing the IRC socket
}
