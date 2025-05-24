// admin.c - Stub for admin command handling
#include "admin.h"
#include <string.h>
#include <strings.h>

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
