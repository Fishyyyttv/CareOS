/* tests/host/test_term_cmds.c -- help grid, "did you mean" and completion. */
#include "pure.h"
#include "term_cmds.h"
#include <assert.h>
#include <string.h>

static void test_help_grid(void) {
    char line[160];
    /* Every command belongs to a real group and appears in its card. */
    for (u32 i = 0; i < TERM_CMD_COUNT; i++) {
        assert(TERM_CMDS[i].group < TG_COUNT);
        term_help_line(TERM_CMDS[i].group, line, sizeof line);
        assert(strstr(line, TERM_CMDS[i].name));
    }
    term_help_line(TG_FILES, line, sizeof line);
    assert(!strncmp(line, "ls cd pwd cat", 13));
    term_help_line(TG_CARE, line, sizeof line);
    assert(!strcmp(line, "care <file.cl> help"));
    assert(!strcmp(term_group_name(TG_CARE), "Care language"));
    /* truncation stays terminated */
    assert(term_help_line(TG_FILES, line, 6) == 5 && line[5] == '\0');
}

static void test_distance(void) {
    assert(term_edit_distance("ls", "ls", 2) == 0);
    assert(term_edit_distance("sl", "ls", 2) == 1);        /* swap */
    assert(term_edit_distance("claer", "clear", 2) == 1);
    assert(term_edit_distance("pnig", "ping", 2) == 1);
    assert(term_edit_distance("ifconfg", "ifconfig", 2) == 1);
    assert(term_edit_distance("abcdef", "x", 2) == 3);     /* gave up */
    assert(term_edit_distance("kitten", "sitting", 5) == 3);
}

static void test_suggest(void) {
    assert(!strcmp(term_suggest_cmd("claer"), "clear"));
    assert(!strcmp(term_suggest_cmd("sl"), "ls"));
    assert(!strcmp(term_suggest_cmd("pnig"), "ping"));
    assert(!strcmp(term_suggest_cmd("netwrok"), "network"));
    assert(!strcmp(term_suggest_cmd("udptime"), "uptime"));
    assert(term_suggest_cmd("ls") == NULL);                /* exact */
    assert(term_suggest_cmd("x") == NULL);                 /* too short to guess */
    assert(term_suggest_cmd("frobnicate") == NULL);
    assert(term_suggest_cmd("") == NULL && term_suggest_cmd(NULL) == NULL);
    /* "ct": cat and cd are both 1 away; same first letter wins, then order */
    assert(!strcmp(term_suggest_cmd("ct"), "cd"));
}

static void test_completion(void) {
    u32 idx[16];
    u32 n = term_comp_match("net", TERM_COMPS, TERM_COMP_COUNT, idx, 16);
    assert(n == 2);
    assert(!strcmp(TERM_COMPS[idx[0]].text, "network --status"));
    assert(!strcmp(TERM_COMPS[idx[1]].text, "network --ip"));
    assert(term_comp_common(TERM_COMPS, idx, n) == 10);   /* "network --" */
    n = term_comp_match("pw", TERM_COMPS, TERM_COMP_COUNT, idx, 16);
    assert(n == 1 && !strcmp(TERM_COMPS[idx[0]].text, "pwd"));
    /* complete input is not offered again */
    assert(term_comp_match("pwd", TERM_COMPS, TERM_COMP_COUNT, idx, 16) == 0);
    /* empty input lists everything; max caps writes but not the count */
    assert(term_comp_match("", TERM_COMPS, TERM_COMP_COUNT, idx, 4) == TERM_COMP_COUNT);
    assert(term_comp_match("zz", TERM_COMPS, TERM_COMP_COUNT, idx, 16) == 0);
    assert(term_comp_common(TERM_COMPS, idx, 0) == 0);
    /* every completion's first word is a real command */
    for (u32 i = 0; i < TERM_COMP_COUNT; i++) {
        char w[16]; u32 k = 0;
        while (TERM_COMPS[i].text[k] && TERM_COMPS[i].text[k] != ' ' && k < 15) { w[k] = TERM_COMPS[i].text[k]; k++; }
        w[k] = '\0';
        bool found = false;
        for (u32 j = 0; j < TERM_CMD_COUNT; j++) if (!strcmp(TERM_CMDS[j].name, w)) found = true;
        assert(found);
    }
}

int main(void) {
    test_help_grid();
    test_distance();
    test_suggest();
    test_completion();
    return 0;
}
