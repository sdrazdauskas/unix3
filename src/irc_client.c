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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

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
    fflush(stdout);
    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", config->channels[channel_index]);
    send(sockfd, buffer, strlen(buffer), 0);
    usleep(200000); // 200ms delay to avoid flooding
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
                        // Normalize both target and config channel to lowercase for comparison
                        char target_lc[256], config_chan_lc[256];
                        snprintf(target_lc, sizeof(target_lc), "%s", target);
                        snprintf(config_chan_lc, sizeof(config_chan_lc), "%s", config->channels[channel_index]);
                        for (char *p = target_lc; *p; ++p) *p = tolower(*p);
                        for (char *p = config_chan_lc; *p; ++p) *p = tolower(*p);
                        if (strcmp(target_lc, config_chan_lc) == 0) {
                            // Always pass canonical lowercase channel to narrative
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
