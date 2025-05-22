// config.h - Configuration parsing
#ifndef CONFIG_H
#define CONFIG_H
#define MAX_CHANNELS 10
#define MAX_ADMINS 10
#define MAX_STR 128

typedef struct {
    char name[MAX_STR];
    char password[MAX_STR];
} AdminUser;

typedef struct {
    char channels[MAX_CHANNELS][MAX_STR];
    int channel_count;
    AdminUser admins[MAX_ADMINS];
    int admin_count;
    char nickname[MAX_STR];
    char server[MAX_STR];
    int port;
    char narratives_path[MAX_STR];
} BotConfig;

int load_config(const char *path, BotConfig *config);

#endif
