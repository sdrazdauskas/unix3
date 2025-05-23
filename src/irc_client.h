// irc_client.h - IRC protocol handling
#ifndef IRC_CLIENT_H
#define IRC_CLIENT_H
#include "config.h"

void irc_channel_loop(const BotConfig *config, int channel_index, int sockfd);

#endif
