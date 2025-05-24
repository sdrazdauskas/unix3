#ifndef MENTION_H
#define MENTION_H

#include <time.h>
#include "irc_client.h"

#define MAX_PENDING_MENTIONS 8

struct MentionRequest {
    char user[9];      // aaaannnn
    char sender[64];   // who mentioned
    char channel[128]; // channel name
    time_t request_time;
};

// Called to check and handle user mentions in a message
void handle_user_mentions(const BotConfig *config, int channel_index, int sockfd, const char *msg, const char *sender);

// Called to check and handle channel mentions in a message
void handle_channel_mentions(const BotConfig *config, int channel_index, int sockfd, const char *msg, const char *sender);

// Called to handle NAMES reply for user mention alerts
void handle_names_reply(const char *line, int channel_index, int sockfd);

#endif // MENTION_H
