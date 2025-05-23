// narrative.h - Narrative catalogue interface
#ifndef NARRATIVE_H
#define NARRATIVE_H

#define MAX_NARRATIVES 256

typedef struct {
    char channel[64];
    char trigger[128];
    char response[512];
} NarrativeEntry;

extern NarrativeEntry narratives[MAX_NARRATIVES];
extern int narrative_count;

// Loads narratives from a JSON file
int load_narratives(const char *filename);

// Looks up a response for a given channel and message
const char* get_narrative_response(const char* channel, const char* msg);

#endif // NARRATIVE_H
