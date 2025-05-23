// irc_client.c - Minimal IRC client implementation
#include "irc_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

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
        }
    }
    close(sockfd);
}
