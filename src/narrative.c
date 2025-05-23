// narrative.c - Stub for narrative catalogue
#include "narrative.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

NarrativeEntry narratives[MAX_NARRATIVES];
int narrative_count = 0;

// Simple case-insensitive substring search
static int strcasestr_simple(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    size_t nlen = strlen(needle);
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0) return 1;
    }
    return 0;
}

// Loads narratives from a text file: channel|trigger|response per line
int load_narratives(const char *filename) {
    // Trim leading/trailing whitespace from filename
    char fname[512];
    strncpy(fname, filename, sizeof(fname)-1);
    fname[sizeof(fname)-1] = 0;
    // Trim leading
    char *start = fname;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') ++start;
    // Trim trailing
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        --end;
    }
    printf("[DEBUG] Loading narratives from: %s\n", start);
    FILE *f = fopen(start, "r");
    if (!f) {
        perror("[ERROR] fopen");
        return -1;
    }
    char line[1024];
    narrative_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (narrative_count >= MAX_NARRATIVES) break;
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = 0;
        // Skip blank lines and true comments (lines starting with # and no '|')
        if (line[0] == 0) continue;
        if (line[0] == '#' && strchr(line, '|') == NULL) continue;
        // Parse: channel|trigger|response
        char *chan = strtok(line, "|");
        char *trigger = strtok(NULL, "|");
        char *response = strtok(NULL, ""); // rest of line
        if (!chan || !trigger || !response) continue;
        strncpy(narratives[narrative_count].channel, chan, sizeof(narratives[narrative_count].channel)-1);
        narratives[narrative_count].channel[sizeof(narratives[narrative_count].channel)-1] = 0;
        strncpy(narratives[narrative_count].trigger, trigger, sizeof(narratives[narrative_count].trigger)-1);
        narratives[narrative_count].trigger[sizeof(narratives[narrative_count].trigger)-1] = 0;
        strncpy(narratives[narrative_count].response, response, sizeof(narratives[narrative_count].response)-1);
        narratives[narrative_count].response[sizeof(narratives[narrative_count].response)-1] = 0;
        narrative_count++;
    }
    fclose(f);
    return 0;
}

// Looks up a response for a given channel and message
const char* get_narrative_response(const char* channel, const char* msg) {
    for (int i = 0; i < narrative_count; ++i) {
        if (strcasecmp(channel, narratives[i].channel) == 0) {
            if (strcmp(narratives[i].trigger, "*") == 0) {
                // wildcard, always match
                return narratives[i].response;
            }
            if (strcasestr_simple(msg, narratives[i].trigger)) {
                return narratives[i].response;
            }
        }
    }
    return NULL;
}
