#ifndef APPS_LBJEDIT_POS_H
#define APPS_LBJEDIT_POS_H

/* The position store of the LBJ Edit overlay app.
 *
 * LBJ Edit only edits: it keeps five positions in its own config area (the bytes
 * cfg_load/cfg_save move, APP_CONFIG_LOCAL_MAX of them) and marks one of them as
 * the primary.  The resident LBJ popup reads that primary position to place the
 * receiver when it works out the distance to a decoded train, so there is one
 * copy the user edits in one place and no resident menu of its own.
 *
 * Each slot is the same 13 decimal digits APRS Edit uses - latitude DD MM hh
 * then longitude DDD MM hh, hundredths of a minute, north and east only - so an
 * erased slot is simply an invalid one and the resident needs no conversion it
 * does not already have for the APRS TX profile.  1e-4 degrees come out of
 * degrees * 10000 + (MMhh * 10000) / 6000, as in lbj_dual.c. */

#include <stdint.h>

#define LBJ_POS_MAGIC    0x314A424Cu /* "LBJ1" */
#define LBJ_POS_VERSION  1u
#define LBJ_POS_SLOTS    5u
#define LBJ_POS_DIGITS   13u

#define LBJ_POS_VER      4u          /* version byte                            */
#define LBJ_POS_PRIMARY  5u          /* which slot the popup must use           */
#define LBJ_POS_NONE     0xFFu       /* that byte: no slot chosen               */
#define LBJ_POS_FIRST    8u          /* the first slot's 13 digits              */
#define LBJ_POS_LEN      (LBJ_POS_FIRST + LBJ_POS_SLOTS * LBJ_POS_DIGITS)

#endif /* APPS_LBJEDIT_POS_H */
