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

void irc_channel_loop(const BotConfig *config, int channel_index) {
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[512];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;

    // Remove leading/trailing whitespace from config->server
    char server_addr[256];
    strncpy(server_addr, config->server, sizeof(server_addr)-1);
    server_addr[sizeof(server_addr)-1] = '\0';
    // Trim leading whitespace
    char *start = server_addr;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
    // Trim trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        --end;
    }
    // Try to parse as IP address first
    if (inet_aton(start, &serv_addr.sin_addr)) {
        // Parsed as IP, nothing else to do
    } else {
        // Not an IP, try to resolve as hostname
        struct hostent *server = gethostbyname(start);
        if (server == NULL) {
            fprintf(stderr, "ERROR, no such host: %s\n", start);
            exit(1);
        }
        memcpy((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);
    }
    serv_addr.sin_port = htons(config->port);
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }
    // IRC handshake
    snprintf(buffer, sizeof(buffer), "NICK %s\r\n", config->nickname);
    send(sockfd, buffer, strlen(buffer), 0);
    snprintf(buffer, sizeof(buffer), "USER %s 0 * :%s\r\n", config->nickname, config->nickname);
    send(sockfd, buffer, strlen(buffer), 0);
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
