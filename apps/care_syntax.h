/* apps/care_syntax.h -- one-line syntax tokenizer for the Care language
 * (and a C flavour) used by the Files preview and the Editor (pure,
 * host-tested). Line-at-a-time: comments run to the end of the line and
 * there is no multi-line state. */
#ifndef CAREOS_CARE_SYNTAX_H
#define CAREOS_CARE_SYNTAX_H
#include "pure.h"

typedef enum { CS_TEXT = 0, CS_KEYWORD, CS_STRING, CS_NUMBER, CS_COMMENT, CS_PUNCT, CS_KIND_COUNT } cs_kind_t;
typedef enum { CS_LANG_CARE = 0, CS_LANG_C, CS_LANG_PLAIN } cs_lang_t;

typedef struct { u16 start, len; u8 kind; } cs_tok_t;

/* Language from a file name (".cl" Care, ".c"/".h" C, else plain). */
cs_lang_t care_syntax_lang(const char *filename);

/* Split line[0..len) into tokens that cover it exactly, in order (runs of
 * the same kind are merged). Writes up to max; returns the total count. */
u32 care_syntax_line(cs_lang_t lang, const char *line, u32 len, cs_tok_t *out, u32 max);

#endif
