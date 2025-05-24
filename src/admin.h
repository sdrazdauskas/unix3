// admin.h - Admin command interface
#ifndef ADMIN_H
#define ADMIN_H

#include "config.h"
#include "shared_mem.h"

// Placeholder for admin command handling

// Returns 1 if nick is authenticated
int is_authed_admin(const char *nick);
// Add nick to authenticated list
void add_authed_admin(const char *nick);
// Optionally, clear all authed admins (for testing or reload)
void clear_authed_admins(void);
// Returns 1 if a command was handled and should continue, 0 otherwise
int handle_admin_command(const char *sender, const char *msg, const BotConfig *config, int sockfd, AdminState *admin_state);

#endif
