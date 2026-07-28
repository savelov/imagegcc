/* C side of the Qt bridge: hands the rendered GRX surface to the front end. */

#include <stddef.h>
#include <string.h>
#include <grx20.h>

#include "qt_bridge.h"

static GrContext *screen_ctx = NULL;

void qt_set_screen_context(void *ctx)
{
    screen_ctx = (GrContext *)ctx;
}

unsigned char *qt_screen_pixels(void)
{
    if (screen_ctx == NULL) return NULL;
    return (unsigned char *)screen_ctx->gc_frame.gf_baseaddr[0];
}

int qt_screen_width(void)
{
    return screen_ctx ? screen_ctx->gc_xmax + 1 : 0;
}

int qt_screen_height(void)
{
    return screen_ctx ? screen_ctx->gc_ymax + 1 : 0;
}

int qt_screen_stride(void)
{
    return screen_ctx ? screen_ctx->gc_frame.gf_lineoffset : 0;
}

/* the map, drawn by draw_map() into its own buffer */
extern GrContext *RamContext;
extern int WINDOW_LEFT, WINDOW_UP;

unsigned char *qt_map_pixels(void)
{
    if (RamContext == NULL) return NULL;
    return (unsigned char *)RamContext->gc_frame.gf_baseaddr[0];
}

int qt_map_width(void)
{
    return RamContext ? RamContext->gc_xmax + 1 : 0;
}

int qt_map_height(void)
{
    return RamContext ? RamContext->gc_ymax + 1 : 0;
}

int qt_map_stride(void)
{
    return RamContext ? RamContext->gc_frame.gf_lineoffset : 0;
}

int qt_map_origin_x(void) { return WINDOW_LEFT; }
int qt_map_origin_y(void) { return WINDOW_UP; }

void qt_compose_map(void)
{
    unsigned char *dst = qt_screen_pixels();
    unsigned char *src = qt_map_pixels();
    int dst_stride = qt_screen_stride();
    int src_stride = qt_map_stride();
    int width  = qt_map_width();
    int height = qt_map_height();
    int x = WINDOW_LEFT, y = WINDOW_UP;
    int row, bytes;

    if (dst == NULL || src == NULL) return;

    /* the map is taller than the screen leaves room for, so clip it */
    if (x + width  > qt_screen_width())  width  = qt_screen_width()  - x;
    if (y + height > qt_screen_height()) height = qt_screen_height() - y;
    if (width <= 0 || height <= 0) return;

    bytes = width * 3;                  /* both surfaces are 24 bits deep */
    for (row = 0; row < height; row++)
        memcpy(dst + (long)(y + row) * dst_stride + (long)x * 3,
               src + (long)row * src_stride, bytes);
}
