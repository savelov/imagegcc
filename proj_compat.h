/* Minimal replacement for the PROJ.4 <proj_api.h> interface.
 *
 * proj_api.h and the pj_* entry points were removed in PROJ 8.  Only the
 * three calls this project uses are provided here; they are implemented in
 * proj_compat.c on top of the current PROJ C API.
 */

#ifndef PROJ_COMPAT_H
#define PROJ_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void *projPJ;

/* proj_api.h used to supply these */
#ifndef DEG_TO_RAD
#define DEG_TO_RAD .0174532925199432958
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.29577951308232
#endif

projPJ pj_init_plus(const char *definition);
int    pj_transform(projPJ src, projPJ dst, long point_count, int point_offset,
                    double *x, double *y, double *z);
void   pj_free(projPJ pj);

#ifdef __cplusplus
}
#endif

#endif /* PROJ_COMPAT_H */
