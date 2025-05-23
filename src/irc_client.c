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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// Helper: simple narrative lookup for demonstration
const char* get_narrative_response(const char* channel, const char* msg) {
    // #unix
    if (strcmp(channel, "#unix") == 0) {
        if (strstr(msg, "ls"))
            return "'ls' lists directory contents. Try 'ls -l' for more details.";
        if (strstr(msg, "hello"))
            return "Hello! This is the Unix channel. Ask me anything about Unix!";
        return "I'm here to help with Unix questions.";
    }
    // #random
    if (strcmp(channel, "#random") == 0) {
        if (strstr(msg, "hello"))
            return "Hey there! Welcome to #random.";
        return "Let's talk about anything!";
    }
    // #admin
    if (strcmp(channel, "#admin") == 0) {
        return "Admin channel. Use commands to control the bot.";
    }
    return NULL;
}

void irc_channel_loop(const BotConfig *config, int channel_index, int sockfd) {
    char buffer[512];
    // Join the assigned channel
    snprintf(buffer, sizeof(buffer), "JOIN %s\r\n", config->channels[channel_index]);
    send(sockfd, buffer, strlen(buffer), 0);
    // Main loop: print server messages
    while (1) {
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
        // Respond to PRIVMSG in the forked child process
        char *privmsg = strstr(buffer, "PRIVMSG");
        if (privmsg) {
            // Extract channel and message
            char *channel = strtok(privmsg + 8, " ");
            char *msg = strchr(privmsg, ':');
            if (msg) {
                msg++; // skip ':'
                // Use catalogue for response
                if (strcmp(channel, config->channels[channel_index]) == 0) {
                    const char* reply_text = get_narrative_response(channel, msg);
                    if (reply_text) {
                        char reply[512];
                        snprintf(reply, sizeof(reply), "PRIVMSG %s :%s\r\n", channel, reply_text);
                        send(sockfd, reply, strlen(reply), 0);
                    }
                }
            }
        }
    }
    close(sockfd);
}
