/* apps/term_cmds.h -- Terminal command table, "did you mean" ranking and the
 * completion model (pure, host-tested). The help grid, suggestions and Tab
 * completion are all built from TERM_CMDS, so they cannot drift apart. */
#ifndef CAREOS_TERM_CMDS_H
#define CAREOS_TERM_CMDS_H
#include "pure.h"

enum { TG_FILES = 0, TG_NETWORK, TG_SYSTEM, TG_CAREOS, TG_LOOK, TG_CARE, TG_COUNT };

typedef struct {
    const char *name;   /* what you type */
    u8          group;  /* TG_* */
    const char *hint;   /* short description (completion hint column) */
} term_cmd_t;

/* A completion candidate: the full text it completes to, plus a hint. */
typedef struct { const char *text; const char *hint; } term_comp_t;

extern const term_cmd_t  TERM_CMDS[];
extern const u32         TERM_CMD_COUNT;
extern const term_comp_t TERM_COMPS[];     /* commands plus common flag forms */
extern const u32         TERM_COMP_COUNT;

const char *term_group_name(u32 group);

/* The help card text for one group: command names joined by spaces
 * ("care <file.cl>" for the Care language group). Returns the length. */
u32 term_help_line(u32 group, char *out, u32 cap);

/* Optimal-string-alignment distance (a swap of neighbours costs 1), giving
 * up and returning limit+1 once every alignment exceeds limit. */
u32 term_edit_distance(const char *a, const char *b, u32 limit);

/* Best command for a mistyped name: distance <= 2 and smaller than the
 * typed length; ties prefer the same first letter, then table order.
 * NULL when there is no close match (or the name is already a command). */
const char *term_suggest_cmd(const char *typed);

/* Candidates whose text starts with input and is longer than it, in source
 * order. Writes up to max indices; returns the total number of matches. */
u32 term_comp_match(const char *input, const term_comp_t *src, u32 n, u32 *out, u32 max);

/* Length of the prefix shared by every listed candidate. */
u32 term_comp_common(const term_comp_t *src, const u32 *idx, u32 n);

#endif
