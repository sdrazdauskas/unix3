// admin.c - Stub for admin command handling
#include "admin.h"
#include "irc_client.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

#define MAX_AUTHED 10
static char authed_admins[MAX_AUTHED][64];
static int authed_count = 0;

int is_authed_admin(const char *nick) {
    for (int i = 0; i < authed_count; ++i) {
        if (strcasecmp(authed_admins[i], nick) == 0) return 1;
    }
    return 0;
}

void add_authed_admin(const char *nick) {
    if (!is_authed_admin(nick) && authed_count < MAX_AUTHED) {
        strncpy(authed_admins[authed_count], nick, 63);
        authed_admins[authed_count][63] = 0;
        authed_count++;
    }
}

void clear_authed_admins(void) {
    authed_count = 0;
}

// Returns 1 if a command was handled and should continue, 0 otherwise
int handle_admin_command(const char *sender, const char *msg, const BotConfig *config, int sockfd, AdminState *admin_state) {
    if (!is_authed_admin(sender)) {
        char warnmsg[256];
        snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You must authenticate with /msg %s !auth password before using admin commands.\r\n", config->nickname);
        send_irc_message(sockfd, warnmsg);
        return 1;
    }
    if (strncmp(msg, "!stop ", 6) == 0) {
        char *chan = (char*)msg + 6;
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
        return 1;
    } else if (strncmp(msg, "!start ", 7) == 0) {
        char *chan = (char*)msg + 7;
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
        return 1;
    } else if (strncmp(msg, "!ignore ", 8) == 0) {
        add_ignored_user(msg+8);
        printf("[ADMIN] Now ignoring: %s\n", msg+8);
        char adminmsg[256];
        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Now ignoring user: %s\r\n", msg+8);
        send_irc_message(sockfd, adminmsg);
        return 1;
    } else if (strncmp(msg, "!removeignore ", 14) == 0) {
        remove_ignored_user(msg+14);
        printf("[ADMIN] Ignore removed for: %s\n", msg+14);
        char adminmsg[256];
        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Ignore removed for user: %s\r\n", msg+14);
        send_irc_message(sockfd, adminmsg);
        return 1;
    } else if (strncmp(msg, "!clearignore", 12) == 0) {
        clear_ignored_users();
        printf("[ADMIN] All ignores cleared.\n");
        char adminmsg[256];
        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :All ignores cleared.\r\n");
        send_irc_message(sockfd, adminmsg);
        return 1;
    } else if (strncmp(msg, "!topic ", 7) == 0) {
        strncpy(admin_state->current_topic, msg+7, sizeof(admin_state->current_topic)-1);
        admin_state->current_topic[sizeof(admin_state->current_topic)-1] = 0;
        printf("[ADMIN] Topic changed to: %s\n", admin_state->current_topic);
        return 1;
    }
    // If authenticated but not a recognized command, send a prompt
    char warnmsg[256];
    snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You are authenticated. Enter your admin command.\r\n");
    send_irc_message(sockfd, warnmsg);
    return 1;
}
