/* Bridge between the Qt front end (C++) and the drawing core (C).
 *
 * The core still renders with GRX, but into a memory context instead of an
 * X11 screen; the Qt canvas just shows that surface.  Nothing here pulls in
 * grx20.h or image.h, so the C++ side does not have to deal with either.
 */

#ifndef QT_BRIDGE_H
#define QT_BRIDGE_H

/* Size of the virtual screen the core draws into.  It used to be the fixed
 * geometry the X11 full screen mode had, which put a ceiling on how large the
 * map could ever be; main() now sets it from the display before image_init(),
 * and these are only the fallback for a run with no screen to ask. */
#ifndef QT_SCREEN_XSIZE
#define QT_SCREEN_XSIZE 1920
#endif
#ifndef QT_SCREEN_YSIZE
#define QT_SCREEN_YSIZE 1080
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Call before image_init().  The surface is allocated once, so this is the
 * upper bound on the map for the life of the process. */
void qt_set_screen_size(int w, int h);
int  qt_surface_width(void);
int  qt_surface_height(void);

/* Grow or shrink the map area to w by h, within what the surface and the
 * readout column leave room for.  Returns 1 if the geometry changed, so the
 * caller knows to re-wrap the framebuffers and repaint. */
int  qt_resize_map(int w, int h);

/* repaint, for the resize path; declared here so the C++ side need not
 * include image.h */
int  draw_map(int vectors);

/* called by init_graph() once the memory context exists */
void qt_set_screen_context(void *ctx);

/* The panels (legend, readouts, frame) are drawn on the screen surface and
 * the map itself into a separate buffer; the canvas draws the map over the
 * screen at qt_map_origin_*.  Both are 24 bits per pixel and the stride may
 * exceed width*3. */
unsigned char *qt_screen_pixels(void);
int qt_screen_width(void);
int qt_screen_height(void);
int qt_screen_stride(void);

unsigned char *qt_map_pixels(void);
int qt_map_width(void);
int qt_map_height(void);
int qt_map_stride(void);
int qt_map_origin_x(void);
int qt_map_origin_y(void);

/* Copy the map into the screen surface.  draw_map() calls this so that
 * whatever the core draws afterwards - the archive window, above all -
 * lands on top of the map instead of behind it. */
void qt_compose_map(void);

int  qt_screen_bpp(void);
int  qt_map_bpp(void);
void qt_dump_surfaces(void);

/* where the fixed panes live on the surface (defined in showmap.c/showdata.c) */
/* vertical cross section, driven by the left mouse button (showmap.c) */
void cross_section_click(void);
int  cross_section_state(void);

void qt_legend_rect(int *x,int *y,int *w,int *h);
void qt_readout_rect(int *x,int *y,int *w,int *h);
void qt_redraw_readout(void);

/* Blocking key read for the archive browser, served by the Qt event loop.
 * Implemented in qtmain.cpp; returns the same codes GetWindowKey() did. */
int qt_wait_key(void);

/* Non blocking counterpart, called by draw_map() between animation frames:
 * repaints and returns a pending key, or 0.  animate() stops on space. */
int qt_poll_key(void);

/* The product table, for building the panel.  There are more than forty
 * products now, so the buttons are generated from the table rather than
 * listed by hand.  The labels are not exported: descr is CP866, and the C++
 * side writes its own from the family and the level.  Keep these in step
 * with enum product_families in image.h. */
enum {
   QT_FAM_DBZ = 0, QT_FAM_RAIN, QT_FAM_SUM, QT_FAM_ZDR, QT_FAM_VEL,
   QT_FAM_HEIGHT, QT_FAM_PHENOM, QT_FAM_COUNT
};
int product_count(void);
int product_family_of(int index);
int product_level_of(int index);
int product_key_of(int index);      /* feed to key_pressed() */
int map_present(int index);         /* the file on screen carries it */

/* entry points of the existing program */
int  image_init(int argc, char *argv[]);   /* was main() */
int  key_pressed(int key);                 /* 1 means quit */
void mouse_move(int x, int y);
int  mouse_click_left(int x, int y);
void timer(void);
void close_graph(void);

#ifdef __cplusplus
}
#endif

#endif /* QT_BRIDGE_H */
