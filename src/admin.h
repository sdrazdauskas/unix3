// admin.h - Admin command interface
#ifndef ADMIN_H
#define ADMIN_H

// Placeholder for admin command handling

// Returns 1 if nick is authenticated
int is_authed_admin(const char *nick);
// Add nick to authenticated list
void add_authed_admin(const char *nick);
// Optionally, clear all authed admins (for testing or reload)
void clear_authed_admins(void);

#endif
