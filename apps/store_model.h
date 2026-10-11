/* apps/store_model.h -- the Packages app's model (pure, host-tested):
 * .care manifest headers, version ordering, how a package file relates to
 * what is installed, and which state the Discover page is in. CareOS has
 * no package server, so "available" means .care files on this computer. */
#ifndef CAREOS_STORE_MODEL_H
#define CAREOS_STORE_MODEL_H
#include "pure.h"

typedef struct {
    char name[32];
    char version[16];
    char description[96];
    char category[32];
    char author[32];
} care_manifest_t;

/* Parse the header of a .care file ("CARE 1.0" then key=value lines up to
 * ---FILES---). False unless it starts with "CARE " and has a name. */
bool care_manifest_parse(const char *text, u32 len, care_manifest_t *m);

/* Compare dotted numeric versions ("1.10.0" > "1.9.2"); missing parts are
 * 0 and anything after a non-digit in a part is ignored. <0, 0, >0. */
int care_version_cmp(const char *a, const char *b);

typedef enum { PKG_NEW = 0, PKG_UPDATE, PKG_SAME, PKG_OLDER } pkg_rel_t;
/* A package file versus the installed copy (installed_version NULL = not installed). */
pkg_rel_t store_relation(const char *installed_version, const char *file_version);

typedef struct { u32 installed, fresh, updates; } store_counts_t;
typedef enum { STORE_EMPTY = 0, STORE_NOTHING_NEW, STORE_NEW, STORE_UPDATES } store_state_t;
/* Updates win, then new packages, then "all installed", else empty. */
store_state_t store_state(store_counts_t c);

#endif
