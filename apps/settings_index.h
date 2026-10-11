/* apps/settings_index.h -- Settings sections and the sidebar search filter
 * (pure, host-tested). Only sections backed by real settings exist. */
#ifndef CAREOS_SETTINGS_INDEX_H
#define CAREOS_SETTINGS_INDEX_H
#include "pure.h"

enum {
    SET_NETWORK = 0, SET_DISPLAY, SET_APPEARANCE, SET_SOUND, SET_NOTIFY,
    SET_DATETIME, SET_ACCOUNTS, SET_STORAGE, SET_PIP, SET_ABOUT, SET_COUNT
};

typedef struct {
    const char *title;
    const char *keywords;   /* space-separated extra search terms */
} settings_section_t;

extern const settings_section_t SETTINGS_SECTIONS[SET_COUNT];

/* True when every word of query appears (case-insensitively) in the
 * section's title or keywords. An empty or blank query matches all. */
bool settings_section_matches(u32 section, const char *query);

/* Matching sections in order; writes up to max, returns the count. */
u32 settings_filter(const char *query, u32 *out, u32 max);

#endif
