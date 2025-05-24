// irc_client.h - IRC protocol handling
#ifndef IRC_CLIENT_H
#define IRC_CLIENT_H
#include "config.h"

void irc_channel_loop(const BotConfig *config, int channel_index, int sockfd, int pipe_fd);
void send_irc_message(int sockfd, const char *msg);
int is_ignored_user(const char *nick);
void add_ignored_user(const char *nick);
void remove_ignored_user(const char *nick);
void clear_ignored_users(void);

#endif
