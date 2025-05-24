// config.c - Configuration parsing implementation
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int load_config(const char *path, BotConfig *config) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    config->channel_count = 0;
    config->admin_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "channels =", 10) == 0) {
            char *p = strchr(line, '=') + 1;
            // Remove whitespace and split by comma
            char *tok = strtok(p, ",#\n");
            while (tok && config->channel_count < MAX_CHANNELS) {
                // Trim leading/trailing whitespace from tok
                while (*tok == ' ' || *tok == '\t') ++tok;
                char *end = tok + strlen(tok) - 1;
                while (end > tok && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) { *end = 0; --end; }
                if (*tok) {
                    snprintf(config->channels[config->channel_count++], MAX_STR, "#%s", tok);
                }
                tok = strtok(NULL, ",#\n");
            }
        } else if (strncmp(line, "admins =", 8) == 0) {
            char *p = strchr(line, '=') + 1;
            char *tok = strtok(p, ",\n");
            while (tok && config->admin_count < MAX_ADMINS) {
                char *sep = strchr(tok, ':');
                if (sep) {
                    *sep = 0;
                    // Trim leading/trailing whitespace for name
                    char *name = tok;
                    while (*name == ' ' || *name == '\t') ++name;
                    char *end = name + strlen(name) - 1;
                    while (end > name && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) { *end = 0; --end; }
                    // Trim leading/trailing whitespace for password
                    char *pass = sep + 1;
                    while (*pass == ' ' || *pass == '\t') ++pass;
                    end = pass + strlen(pass) - 1;
                    while (end > pass && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) { *end = 0; --end; }
                    snprintf(config->admins[config->admin_count].name, MAX_STR, "%s", name);
                    snprintf(config->admins[config->admin_count].password, MAX_STR, "%s", pass);
                    config->admin_count++;
                }
                tok = strtok(NULL, ",\n");
            }
        } else if (strncmp(line, "nickname =", 9) == 0) {
            char *p = strchr(line, '=') + 1;
            // Trim leading whitespace
            while (*p == ' ' || *p == '\t') ++p;
            // Copy and trim trailing whitespace/newline
            snprintf(config->nickname, MAX_STR, "%s", p);
            char *end = config->nickname + strlen(config->nickname) - 1;
            while (end > config->nickname && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
                *end = 0;
                --end;
            }
        } else if (strncmp(line, "server =", 7) == 0) {
            char *p = strchr(line, '=') + 1;
            snprintf(config->server, MAX_STR, "%s", p);
            config->server[strcspn(config->server, "\n")] = 0;
        } else if (strncmp(line, "port =", 6) == 0) {
            char *p = strchr(line, '=') + 1;
            config->port = atoi(p);
        } else if (strncmp(line, "narratives =", 11) == 0) {
            char *p = strchr(line, '=') + 1;
            snprintf(config->narratives_path, MAX_STR, "%s", p);
            config->narratives_path[strcspn(config->narratives_path, "\n")] = 0;
        }
    }
    fclose(f);
    return 0;
}
