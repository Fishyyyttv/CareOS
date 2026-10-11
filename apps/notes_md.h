/* apps/notes_md.h -- the small Markdown subset Notes understands (pure,
 * host-tested). Notes are plain .md files: "# " / "## " headings,
 * "- [ ] " / "- [x] " checklists, "> " quotes, "- " bullets, and inline
 * **bold** / *italic*. */
#ifndef CAREOS_NOTES_MD_H
#define CAREOS_NOTES_MD_H
#include "pure.h"

typedef enum { NL_BLANK = 0, NL_PARA, NL_H1, NL_H2, NL_CHECK, NL_CHECKED, NL_QUOTE, NL_BULLET } nl_kind_t;

/* Kind of one line, and where its text starts after the marker. */
nl_kind_t notes_line_kind(const char *s, u32 len, u32 *content_off);

/* Flip "- [ ]" <-> "- [x]" on the line starting at line_start. Returns
 * false (and changes nothing) when that line is not a checklist item. */
bool notes_toggle_check(char *buf, u32 len, u32 line_start);

/* Add prefix at line_start, or remove it if the line already starts with
 * it. buf holds cap bytes; returns the new length (len when it can't fit). */
u32 notes_toggle_prefix(char *buf, u32 len, u32 cap, u32 line_start, const char *prefix);

/* First non-blank line without its marker ("Untitled" when there is none),
 * and the first non-blank line after it, markers stripped. */
void notes_title(const char *buf, u32 len, char *out, u32 cap);
void notes_snippet(const char *buf, u32 len, char *out, u32 cap);

/* A file name for a title: letters, digits, space, '-' and '_' kept,
 * trimmed, at most cap-4 chars, ".md" appended; "Untitled.md" if empty. */
void notes_file_name(const char *title, char *out, u32 cap);

/* Inline styling: spans of visible text (markers excluded) with
 * style bit 0 = bold, bit 1 = italic. Unclosed markers stay as text. */
typedef struct { u16 start, len; u8 style; } nspan_t;
#define NS_BOLD   1u
#define NS_ITALIC 2u
u32 notes_spans(const char *s, u32 len, nspan_t *out, u32 max);

#endif
