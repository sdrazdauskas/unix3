#include "mention.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "irc_client.h"
#include "utils.h"

static char last_requested_user[9] = "";
static time_t last_request_time = 0;
static char last_request_sender[64] = "";

void handle_user_mentions(const BotConfig *config, int channel_index, int sockfd, const char *msg, const char *sender) {
    for (const char *p = msg; *p; ++p) {
        if (strlen(p) < 8) break;
        int is_user = 1;
        for (int i = 0; i < 4; ++i) {
            if (!isalpha((unsigned char)p[i])) { is_user = 0; break; }
        }
        for (int i = 4; i < 8; ++i) {
            if (!isdigit((unsigned char)p[i])) { is_user = 0; break; }
        }
        int start_ok = (p == msg) || !isalnum((unsigned char)*(p-1));
        int end_ok = !isalnum((unsigned char)p[8]);
        if (is_user && start_ok && end_ok) {
            char user[9];
            strncpy(user, p, 8); user[8] = 0;
            if (strcasecmp(sender, user) == 0) continue;
            printf("[DEBUG] Username mention detected: '%s' by '%s' in %s\n", user, sender, config->channels[channel_index]);
            char names_cmd[256];
            snprintf(names_cmd, sizeof(names_cmd), "NAMES %s\r\n", config->channels[channel_index]);
            send_irc_message(sockfd, names_cmd);
            strncpy(last_requested_user, user, 9);
            last_requested_user[8] = 0;
            last_request_time = time(NULL);
            strncpy(last_request_sender, sender, sizeof(last_request_sender)-1);
            last_request_sender[sizeof(last_request_sender)-1] = 0;
            printf("[DEBUG] Requested NAMES for %s to check if %s is present\n", config->channels[channel_index], user);
        }
    }
}

void handle_channel_mentions(const BotConfig *config, int channel_index, int sockfd, const char *msg, const char *sender) {
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
}

void handle_names_reply(const char *line, int channel_index, int sockfd) {
    char *last_chan_start = strchr(line, '#');
    char *last_colon = strrchr(line, ':');
    if (last_chan_start && last_colon && last_colon > last_chan_start) {
        char users[512];
        strncpy(users, last_colon + 1, sizeof(users) - 1);
        users[sizeof(users) - 1] = 0;
        char *tok = strtok(users, " ");
        char last_found_user[16];
        int user_found = 0;
        // Check if the last requested user is in the NAMES reply
        while (tok) {
            if (strcasecmp(tok, last_requested_user) == 0) {
                user_found = 1;
                strncpy(last_found_user, tok, sizeof(last_found_user)-1);
                last_found_user[sizeof(last_found_user)-1] = 0;
                break;
            }
            tok = strtok(NULL, " ");
        }
        // If user not found and request is recent, send alert
        if (!user_found && last_requested_user[0] && (time(NULL) - last_request_time) < 5) {
            // Extract only the channel name (up to first space or end)
            char channel_name[128] = "";
            size_t i = 0;
            while (last_chan_start[i] && !isspace((unsigned char)last_chan_start[i]) && i < sizeof(channel_name)-1) {
                channel_name[i] = last_chan_start[i];
                i++;
            }
            channel_name[i] = 0;
            char privmsg[512];
            snprintf(privmsg, sizeof(privmsg), "PRIVMSG %s :[ALERT] %s mentioned you in %s.\r\n", last_requested_user, last_request_sender, channel_name);
            send_irc_message(sockfd, privmsg);
            printf("[CHILD %d] Sent alert to %s (not present in %s)\n", channel_index, last_requested_user, channel_name);
            last_requested_user[0] = 0;
            last_request_sender[0] = 0;
        }
    }
}
