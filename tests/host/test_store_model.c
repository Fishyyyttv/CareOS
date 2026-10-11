/* tests/host/test_store_model.c -- Packages model. */
#include "pure.h"
#include "store_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    care_manifest_t m;
    const char *ok =
        "CARE 1.0\n"
        "name=quickcalc\n"
        "version=0.1.0\n"
        "description= Quick arithmetic helper \n"
        "author=CareOS Community\r\n"
        "category=Utilities\n"
        "---FILES---\n"
        "FILE calc.sh\n"
        "name=not-a-header\n";
    assert(care_manifest_parse(ok, (u32)strlen(ok), &m));
    assert(!strcmp(m.name, "quickcalc") && !strcmp(m.version, "0.1.0"));
    assert(!strcmp(m.description, "Quick arithmetic helper"));
    assert(!strcmp(m.author, "CareOS Community") && !strcmp(m.category, "Utilities"));

    assert(!care_manifest_parse("hello", 5, &m));                 /* not a .care */
    const char *noname = "CARE 1.0\nversion=1\n---FILES---\n";
    assert(!care_manifest_parse(noname, (u32)strlen(noname), &m));
    const char *nover = "CARE 1.0\nname=x\n";
    assert(care_manifest_parse(nover, (u32)strlen(nover), &m) && !strcmp(m.version, "0"));
    char longname[200] = "CARE 1.0\nname=";
    for (int i = 0; i < 60; i++) strcat(longname, "a");
    assert(care_manifest_parse(longname, (u32)strlen(longname), &m) && strlen(m.name) == 31);

    assert(care_version_cmp("1.10.0", "1.9.2") > 0);
    assert(care_version_cmp("1.0", "1.0.0") == 0);
    assert(care_version_cmp("0.1.0", "0.2") < 0);
    assert(care_version_cmp("2", "10") < 0);
    assert(care_version_cmp("1.2.3-beta", "1.2.3") == 0);
    assert(care_version_cmp(NULL, "0") == 0);

    assert(store_relation(NULL, "1.0") == PKG_NEW);
    assert(store_relation("1.0", "1.1") == PKG_UPDATE);
    assert(store_relation("1.1", "1.1.0") == PKG_SAME);
    assert(store_relation("2.0", "1.9") == PKG_OLDER);

    store_counts_t c = {0, 0, 0};
    assert(store_state(c) == STORE_EMPTY);
    c.installed = 2;
    assert(store_state(c) == STORE_NOTHING_NEW);
    c.fresh = 1;
    assert(store_state(c) == STORE_NEW);
    c.updates = 1;
    assert(store_state(c) == STORE_UPDATES);
    c.installed = 0; c.fresh = 0;
    assert(store_state(c) == STORE_UPDATES);
    puts("store_model OK");
    return 0;
}
