/* tests/host/test_notes_md.c -- Notes Markdown subset. */
#include "pure.h"
#include "notes_md.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static nl_kind_t K(const char *s, u32 *off) { return notes_line_kind(s, (u32)strlen(s), off); }

int main(void) {
    u32 off;
    assert(K("", &off) == NL_BLANK && K("   ", &off) == NL_BLANK);
    assert(K("# Title", &off) == NL_H1 && off == 2);
    assert(K("## Sub", &off) == NL_H2 && off == 3);
    assert(K("- [ ] task", &off) == NL_CHECK && off == 6);
    assert(K("- [x] done", &off) == NL_CHECKED && K("- [X] done", &off) == NL_CHECKED);
    assert(K("- [ ]", &off) == NL_CHECK && off == 5);
    assert(K("> quote", &off) == NL_QUOTE && off == 2);
    assert(K("- item", &off) == NL_BULLET && K("* item", &off) == NL_BULLET);
    assert(K("#hashtag", &off) == NL_PARA && K("-dash", &off) == NL_PARA);

    /* checklist toggle round-trip leaves everything else intact */
    char b[128] = "# List\n- [ ] one\n- [x] two\nplain";
    u32 len = (u32)strlen(b);
    char orig[128]; strcpy(orig, b);
    assert(notes_toggle_check(b, len, 7));
    assert(!strncmp(b + 7, "- [x] one", 9));
    assert(notes_toggle_check(b, len, 17));
    assert(!strncmp(b + 17, "- [ ] two", 9));
    assert(notes_toggle_check(b, len, 7) && notes_toggle_check(b, len, 17));
    assert(!strcmp(b, orig));
    assert(!notes_toggle_check(b, len, 0));            /* heading: untouched */
    assert(!notes_toggle_check(b, len, 27));           /* plain line */
    assert(!strcmp(b, orig));

    /* prefix toggle adds and removes */
    char c[64] = "first\nsecond";
    u32 l = (u32)strlen(c);
    l = notes_toggle_prefix(c, l, sizeof c, 6, "## ");
    assert(!strcmp(c, "first\n## second") && l == strlen(c));
    l = notes_toggle_prefix(c, l, sizeof c, 6, "## ");
    assert(!strcmp(c, "first\nsecond") && l == strlen(c));
    l = notes_toggle_prefix(c, l, sizeof c, 0, "- [ ] ");
    assert(!strcmp(c, "- [ ] first\nsecond"));
    char tight[8] = "abcdef";                          /* no room: unchanged */
    assert(notes_toggle_prefix(tight, 6, sizeof tight, 0, "## ") == 6 && !strcmp(tight, "abcdef"));

    /* title and snippet */
    char t[32], sn[64];
    const char *note = "\n# v9.1 **launch** list\n\nEverything that needs to land.\n- [ ] x";
    notes_title(note, (u32)strlen(note), t, sizeof t);
    assert(!strcmp(t, "v9.1 launch list"));
    notes_snippet(note, (u32)strlen(note), sn, sizeof sn);
    assert(!strcmp(sn, "Everything that needs to land."));
    notes_title("", 0, t, sizeof t);
    assert(!strcmp(t, "Untitled"));
    notes_snippet("only a title", 12, sn, sizeof sn);
    assert(sn[0] == '\0');

    /* file names */
    char fn[32];
    notes_file_name("v9.1 launch list!", fn, sizeof fn);
    assert(!strcmp(fn, "v91 launch list.md"));
    notes_file_name("  ??  ", fn, sizeof fn);
    assert(!strcmp(fn, "Untitled.md"));
    notes_file_name("a very long title that will not fit at all", fn, 16);
    assert(strlen(fn) <= 15 && !strcmp(fn + strlen(fn) - 3, ".md"));

    /* inline spans */
    nspan_t sp[8];
    const char *s1 = "a **b** *c* d";
    u32 n = notes_spans(s1, (u32)strlen(s1), sp, 8);
    assert(n == 5);
    assert(sp[1].style == NS_BOLD && !strncmp(s1 + sp[1].start, "b", sp[1].len));
    assert(sp[3].style == NS_ITALIC && !strncmp(s1 + sp[3].start, "c", sp[3].len));
    const char *s2 = "2 * 3 = 6";                     /* unclosed: one plain span */
    n = notes_spans(s2, (u32)strlen(s2), sp, 8);
    assert(n == 1 && sp[0].style == 0 && sp[0].len == strlen(s2));
    assert(notes_spans("", 0, sp, 8) == 0);
    puts("notes_md OK");
    return 0;
}
