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
#include <strings.h>
#include <stdbool.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#ifndef HAVE_STRCASESTR
static char *strcasestr(const char *haystack, const char *needle) {
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

static char (*ignored_nicks)[64] = NULL;
static int *ignored_count = NULL;

void set_shared_ignore_ptrs(char (*nicks)[64], int *count) {
    ignored_nicks = nicks;
    ignored_count = count;
}

int is_ignored_user(const char *nick) {
    if (!ignored_nicks || !ignored_count) return 0;
    for (int i = 0; i < *ignored_count; ++i) {
        if (strcasecmp(ignored_nicks[i], nick) == 0) return 1;
    }
    return 0;
}

void add_ignored_user(const char *nick) {
    if (!ignored_nicks || !ignored_count) return;
    if (!is_ignored_user(nick) && *ignored_count < MAX_IGNORED) {
        strncpy(ignored_nicks[*ignored_count], nick, 63);
        ignored_nicks[*ignored_count][63] = 0;
        (*ignored_count)++;
    }
}

void remove_ignored_user(const char *nick) {
    if (!ignored_nicks || !ignored_count) return;
    for (int i = 0; i < *ignored_count; ++i) {
        if (strcasecmp(ignored_nicks[i], nick) == 0) {
            for (int j = i; j < *ignored_count - 1; ++j) {
                strncpy(ignored_nicks[j], ignored_nicks[j+1], 64);
            }
            (*ignored_count)--;
            break;
        }
    }
}

void clear_ignored_users(void) {
    if (ignored_count) *ignored_count = 0;
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
    char last_msg[512] = {0};
    time_t last_msg_time = 0;
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
                // Simple duplicate message/timing check
                time_t now = time(NULL);
                if (strcmp(buffer, last_msg) == 0 && (now - last_msg_time) < 1) {
                    continue;
                }
                strncpy(last_msg, buffer, sizeof(last_msg)-1);
                last_msg[sizeof(last_msg)-1] = 0;
                last_msg_time = now;
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
                        // Skip messages from self (bot)
                        if (strcasecmp(sender, config->nickname) == 0) {
                            line = next;
                            continue;
                        }
                        // Normalize both target and config channel to lowercase for comparison
                        char target_lc[256], config_chan_lc[256];
                        snprintf(target_lc, sizeof(target_lc), "%s", target);
                        snprintf(config_chan_lc, sizeof(config_chan_lc), "%s", config->channels[channel_index]);
                        for (char *p = target_lc; *p; ++p) *p = tolower(*p);
                        for (char *p = config_chan_lc; *p; ++p) *p = tolower(*p);
                        // Admin channel: handle secret commands
                        if (strcmp(config_chan_lc, "#admin") == 0) {
                            // Call the extracted admin command handler
                            if (handle_admin_command(sender, msg, config, sockfd, admin_state)) {
                                continue;
                            }
                        }
                        // For all channels: obey admin state
                        if (strcmp(target_lc, config_chan_lc) == 0) {
                            // If stop_talking is set, do not reply
                            if (admin_state->stop_talking[channel_index]) { line = next; continue; }
                            // If sender is ignored, do not reply
                            if (is_ignored_user(sender)) {
                                printf("[DEBUG] Ignoring user: %s\n", sender);
                                line = next;
                                continue;
                            }
                            // If topic is set, respond to !topic? with the topic
                            if (strncmp(msg, "!topic?", 7) == 0 && admin_state->current_topic[0]) {
                                char reply[512];
                                snprintf(reply, sizeof(reply), "PRIVMSG %s :Current topic: %s\r\n", target, admin_state->current_topic);
                                printf("[CHILD %d] Sending to IRC: %s\n", channel_index, reply);
                                fflush(stdout);
                                send_irc_message(sockfd, reply);
                                line = next; continue;
                            }
                            // Alert if message mentions another channel (word boundary check)
                            for (int i = 0; i < config->channel_count; ++i) {
                                if (i == channel_index) continue;
                                const char *chan_name = config->channels[i];
                                size_t chan_len = strlen(chan_name);
                                const char *p = msg;
                                while ((p = strcasestr(p, chan_name)) != NULL) {
                                    int start_ok = (p == msg) || !isalnum((unsigned char)*(p-1));
                                    int end_ok = !isalnum((unsigned char)*(p+chan_len));
                                    if (start_ok && end_ok) {
                                        char alert[512];
                                        snprintf(alert, sizeof(alert), "PRIVMSG %s :[ALERT] %s mentioned this channel (%s) in %s\r\n", chan_name, sender, chan_name, config->channels[channel_index]);
                                        send_irc_message(sockfd, alert);
                                        break; // Only alert once per channel per message
                                    }
                                    p += chan_len;
                                }
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
