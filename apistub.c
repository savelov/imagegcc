/* apistub.c - the display globals, for the build that has no display.
 *
 * libimage.so is the data path only: read the archive, build the mosaic, write
 * a GeoTIFF (see pyimage.py).  It is compiled with NOGRX and links no graphics
 * driver at all, which is what lets it be a shared object in the first place -
 * GRX ships as a non-PIC static library and cannot go into one.
 *
 * These few globals live in showmap.c and image.c, next to the code that
 * draws.  read_cfg() sets them from image.cfg whether or not anything is going
 * to be drawn, so they have to exist; nothing in the data path reads them
 * back.  The values are the same defaults showmap.c starts with, so a caller
 * that prints them sees something sensible rather than zero.
 */

#include <stdio.h>
#include "image.h"

int WINDOW_XSIZE = 1800;
int WINDOW_YSIZE = 750;
int WINDOW_LEFT  = 100;
float MPIX       = 1;

int Second = 10;                 /* the poll interval of the interactive timer */

/* Every port contributes to the mosaic.  The viewer lets ctrl-F1..F8 drop
 * one; a script that wants a subset can set this before read_files(). */
port_mask show_maps = PORT_ALL;

/* palette.c and files.c report problems here.  The library is a guest in
 * someone else's process, so it complains to stderr rather than picking a
 * file to write. */
FILE *logfile = NULL;

/* stderr is not a constant initialiser on every libc, so bind it late.
 * pyimage.py calls this before anything else. */
void image_api_init(void)
{
   if (logfile == NULL) logfile = stderr;
}
