/* PROJ.4 compatibility shim, implemented with the current PROJ C API.
 *
 * The old pj_* entry points disappeared in PROJ 8, but the coordinate maths
 * this project needs is unchanged, so the calls are simply forwarded to
 * proj_create()/proj_trans().
 *
 * When the PROJ development headers are not installed the handful of
 * declarations we need is provided locally; the library itself is linked by
 * its versioned soname (see make.sh).
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proj_compat.h"

#if defined(__has_include)
# if __has_include(<proj.h>)
#  define HAVE_PROJ_H 1
# endif
#endif

#ifdef HAVE_PROJ_H
# include <proj.h>
#else
/* Subset of proj.h.  PJ_COORD is a union of four doubles in every PROJ
 * release that exports proj_trans(), so the calling convention matches. */
typedef struct PJconsts PJ;
typedef struct pj_ctx PJ_CONTEXT;
typedef union  PJ_COORD { double v[4]; } PJ_COORD;
typedef enum   PJ_DIRECTION { PJ_FWD = 1, PJ_IDENT = 0, PJ_INV = -1 } PJ_DIRECTION;

extern PJ      *proj_create(PJ_CONTEXT *ctx, const char *definition);
extern PJ_COORD proj_trans(PJ *P, PJ_DIRECTION direction, PJ_COORD coord);
extern PJ      *proj_destroy(PJ *P);
extern int      proj_errno(const PJ *P);
#endif

/* A projection plus the one property the old API's behaviour depended on:
 * whether its coordinates are geographic (radians) or projected (metres). */
typedef struct {
    PJ  *pj;
    int  is_latlong;
} compat_pj;

static int definition_is_latlong(const char *definition)
{
    return strstr(definition, "proj=longlat") != NULL ||
           strstr(definition, "proj=latlong") != NULL;
}

projPJ pj_init_plus(const char *definition)
{
    compat_pj *cp;

    if (definition == NULL) return NULL;

    cp = (compat_pj *)malloc(sizeof(*cp));
    if (cp == NULL) return NULL;

    cp->is_latlong = definition_is_latlong(definition);

    /* Geographic "projections" are the identity in radians, which is what the
     * old pj_transform() used them for; no PROJ object is needed. */
    cp->pj = cp->is_latlong ? NULL : proj_create(NULL, definition);

    if (!cp->is_latlong && cp->pj == NULL) {
        free(cp);
        return NULL;
    }
    return (projPJ)cp;
}

void pj_free(projPJ pj)
{
    compat_pj *cp = (compat_pj *)pj;

    if (cp == NULL) return;
    if (cp->pj != NULL) proj_destroy(cp->pj);
    free(cp);
}

/* Transform point_count points, stored with a stride of point_offset, from
 * src to dst.  Geographic coordinates are in radians, as they were with
 * PROJ.4.  Returns 0 on success. */
int pj_transform(projPJ src, projPJ dst, long point_count, int point_offset,
                 double *x, double *y, double *z)
{
    compat_pj *s = (compat_pj *)src;
    compat_pj *d = (compat_pj *)dst;
    long i;
    int  result = 0;

    if (s == NULL || d == NULL || x == NULL || y == NULL) return -1;
    if (point_offset == 0) point_offset = 1;

    for (i = 0; i < point_count; i++) {
        long     k = i * point_offset;
        PJ_COORD c;

        c.v[0] = x[k];
        c.v[1] = y[k];
        c.v[2] = (z != NULL) ? z[k] : 0.0;
        c.v[3] = HUGE_VAL;              /* no epoch */

        /* projected -> geographic (radians) */
        if (!s->is_latlong) c = proj_trans(s->pj, PJ_INV, c);

        /* geographic (radians) -> projected */
        if (!d->is_latlong) c = proj_trans(d->pj, PJ_FWD, c);

        if (c.v[0] == HUGE_VAL || c.v[1] == HUGE_VAL) result = -1;

        x[k] = c.v[0];
        y[k] = c.v[1];
        if (z != NULL) z[k] = c.v[2];
    }
    return result;
}
