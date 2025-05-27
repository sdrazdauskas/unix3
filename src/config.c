// config.c - Configuration parsing implementation
#include "config.h"
#include "utils.h"
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
                trim_whitespace(tok);
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
                    char *name = tok;
                    trim_whitespace(name);
                    char *pass = sep + 1;
                    trim_whitespace(pass);
                    snprintf(config->admins[config->admin_count].name, MAX_STR, "%s", name);
                    snprintf(config->admins[config->admin_count].password, MAX_STR, "%s", pass);
                    config->admin_count++;
                }
                tok = strtok(NULL, ",\n");
            }
        } else if (strncmp(line, "nickname =", 9) == 0) {
            char *p = strchr(line, '=') + 1;
            trim_whitespace(p);
            snprintf(config->nickname, MAX_STR, "%s", p);
        } else if (strncmp(line, "server =", 7) == 0) {
            char *p = strchr(line, '=') + 1;
            trim_whitespace(p);
            snprintf(config->server, MAX_STR, "%s", p);
            config->server[strcspn(config->server, "\n")] = 0;
        } else if (strncmp(line, "port =", 6) == 0) {
            char *p = strchr(line, '=') + 1;
            trim_whitespace(p);
            config->port = atoi(p);
        } else if (strncmp(line, "narratives =", 11) == 0) {
            char *p = strchr(line, '=') + 1;
            trim_whitespace(p);
            snprintf(config->narratives_path, MAX_STR, "%s", p);
            config->narratives_path[strcspn(config->narratives_path, "\n")] = 0;
        } else if (strncmp(line, "logfile =", 8) == 0) {
            char *p = strchr(line, '=') + 1;
            trim_whitespace(p);
            snprintf(config->logfile, MAX_STR, "%s", p);
            config->logfile[strcspn(config->logfile, "\n")] = 0;
        }
    }
    fclose(f);
    return 0;
}
