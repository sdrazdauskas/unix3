// admin.c - Stub for admin command handling
#include "admin.h"
#include "irc_client.h"
#include "shared_mem.h"
#include "utils.h"
#include <string.h>
#include <strings.h>
#include <unistd.h>
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
int handle_admin_command(const char *sender, const char *msg, const BotConfig *config, int sockfd, SharedData *shared_data) {
    // Ignore admin commands from ignored users, except !removeignore
    if (is_ignored_user(sender) && strncmp(msg, "!removeignore ", 14) != 0) {
        printf("[ADMIN] Ignored admin command from: %s\n", sender);
        return 1;
    }
    if (!is_authed_admin(sender)) {
        char warnmsg[256];
        snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :You must authenticate with /msg %s !auth password before using admin commands.\r\n", config->nickname);
        send_irc_message(sockfd, warnmsg);
        return 1;
    }
    if (strncmp(msg, "!stop ", 6) == 0) {
        char *chan = (char*)msg + 6;
        int found = 0;
        for (int i = 0; i < config->channel_count; ++i) {
            if (strcasecmp(chan, config->channels[i]) == 0) {
                shared_data->stop_talking[i] = 1;
                printf("[ADMIN] Stop talking activated for channel: %s\n", chan);
                char adminmsg[256];
                snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Bot will stop talking in %s.\r\n", chan);
                send_irc_message(sockfd, adminmsg);
                found = 1;
                break;
            }
        }
        if (!found) {
            char errmsg[256];
            snprintf(errmsg, sizeof(errmsg), "PRIVMSG #admin :Error: Bot has not joined channel %s.\r\n", chan);
            send_irc_message(sockfd, errmsg);
        }
        return 1;
    } else if (strncmp(msg, "!start ", 7) == 0) {
        char *chan = (char*)msg + 7;
        int found = 0;
        for (int i = 0; i < config->channel_count; ++i) {
            if (strcasecmp(chan, config->channels[i]) == 0) {
                shared_data->stop_talking[i] = 0;
                printf("[ADMIN] Stop talking deactivated for channel: %s\n", chan);
                char adminmsg[256];
                snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Bot will resume talking in %s.\r\n", chan);
                send_irc_message(sockfd, adminmsg);
                found = 1;
                break;
            }
        }
        if (!found) {
            char errmsg[256];
            snprintf(errmsg, sizeof(errmsg), "PRIVMSG #admin :Error: Bot has not joined channel %s.\r\n", chan);
            send_irc_message(sockfd, errmsg);
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
    } else if (strncmp(msg, "!settopic ", 10) == 0) {
        strncpy(shared_data->current_topic, msg+10, sizeof(shared_data->current_topic)-1);
        shared_data->current_topic[sizeof(shared_data->current_topic)-1] = 0;
        printf("[ADMIN] Topic changed to: %s\n", shared_data->current_topic);
        char adminmsg[256];
        const char *adminmsg_prefix = "PRIVMSG #admin :Topic changed to: ";
        snprintf(adminmsg, sizeof(adminmsg), "%s%.*s\r\n", adminmsg_prefix,
            (int)(sizeof(adminmsg) - strlen(adminmsg_prefix) - 3), shared_data->current_topic);
        send_irc_message(sockfd, adminmsg);
        return 1;
    }
    // If authenticated but not a recognized command, send a prompt
    char warnmsg[256];
    snprintf(warnmsg, sizeof(warnmsg), "PRIVMSG #admin :Enter a valid admin command.\r\n");
    send_irc_message(sockfd, warnmsg);
    return 1;
}

// Returns 1 if authentication succeeded, 0 otherwise
int try_admin_auth(const char *sender, const char *password, const BotConfig *config, int sockfd) {
    int found = 0;
    for (int i = 0; i < config->admin_count; ++i) {
        if (strcasecmp(config->admins[i].name, sender) == 0 &&
            strcmp(config->admins[i].password, password) == 0) {
            add_authed_admin(sender);
            found = 1;
            break;
        }
    }
    if (found) {
        printf("[AUTH] %s authenticated as admin.\n", sender);
        log_message("[AUTH] %s authenticated as admin.", sender);
        // Send a private message to the user
        char privmsg[256];
        snprintf(privmsg, sizeof(privmsg), "PRIVMSG %s :Authenticated as admin.\r\n", sender);
        printf("[DEBUG] full PRIVMSG to user: %s", privmsg); // Show the full message
        send_irc_message(sockfd, privmsg);
        // Also send a auth message to #admin channel
        char adminmsg[256];
        snprintf(adminmsg, sizeof(adminmsg), "PRIVMSG #admin :Authenticated admin: %s\r\n", sender);
        send_irc_message(sockfd, adminmsg);
    } else {
        printf("[AUTH] Failed admin auth attempt by: %s\n", sender);
        log_message("[AUTH] Failed admin auth attempt by: %s", sender);
        // Send a private message to the user
        char privmsg[256];
        snprintf(privmsg, sizeof(privmsg), "PRIVMSG %s :Authentication failed.\r\n", sender);
        send_irc_message(sockfd, privmsg);
        // Also send an auth failed message to #admin
        char failmsg[256];
        snprintf(failmsg, sizeof(failmsg), "PRIVMSG #admin :Failed admin auth attempt by: %s\r\n", sender);
        send_irc_message(sockfd, failmsg);
    }
    fflush(stdout);
    usleep(200000);
    return found;
}
