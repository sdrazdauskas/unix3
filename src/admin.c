// admin.c - Stub for admin command handling
#include "admin.h"
#include "irc_client.h"
#include "shared_mem.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

typedef struct {
    char authed_admins[10][64];
    int authed_count;
} SharedAdminAuth;

static SharedAdminAuth *shared_auth = NULL;

void set_shared_admin_auth_ptr(void *ptr) {
    shared_auth = (SharedAdminAuth *)ptr;
}

int is_authed_admin(const char *nick) {
    if (!shared_auth) return 0;
    printf("[ADMIN DEBUG] is_authed_admin: checking '%s' against list:\n", nick);
    for (int i = 0; i < shared_auth->authed_count; ++i) {
        printf("  authed_admins[%d]='%s'\n", i, shared_auth->authed_admins[i]);
        if (strcasecmp(shared_auth->authed_admins[i], nick) == 0) return 1;
    }
    return 0;
}

void add_authed_admin(const char *nick) {
    if (!shared_auth) return;
    if (!is_authed_admin(nick) && shared_auth->authed_count < 10) {
        strncpy(shared_auth->authed_admins[shared_auth->authed_count], nick, 63);
        shared_auth->authed_admins[shared_auth->authed_count][63] = 0;
        shared_auth->authed_count++;
        printf("[ADMIN DEBUG] add_authed_admin: added '%s', authed_count=%d\n", nick, shared_auth->authed_count);
    }
}

void clear_authed_admins(void) {
    if (!shared_auth) return;
    shared_auth->authed_count = 0;
}

// Returns 1 if a command was handled and should continue, 0 otherwise
int handle_admin_command(const char *sender, const char *msg, const BotConfig *config, int sockfd, AdminState *admin_state) {
    // Debug: print sender and message at entry
    printf("[ADMIN DEBUG] sender='%s', msg='%s'\n", sender, msg);
    // Ignore admin commands from ignored users, except !removeignore
    if (is_ignored_user(sender) && strncmp(msg, "!removeignore ", 14) != 0) {
        printf("[ADMIN] Ignored admin command from: %s\n", sender);
        return 1;
    }
    printf("[ADMIN DEBUG] sender not ignored, checking auth\n");
    if (!is_authed_admin(sender)) {
        char warnmsg[256];
        snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You must authenticate with /msg %s !auth password before using admin commands.\r\n", config->nickname);
        send_irc_message(sockfd, warnmsg);
        return 1;
    }
    printf("[ADMIN DEBUG] sender is authed\n");
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
