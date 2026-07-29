/* C side of the Qt bridge: hands the rendered GRX surface to the front end. */

#include <stdio.h>
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

int qt_screen_bpp(void)
{
    return screen_ctx ? screen_ctx->gc_driver->bits_per_pixel : 0;
}

int qt_map_bpp(void)
{
    return RamContext ? RamContext->gc_driver->bits_per_pixel : 0;
}

void qt_compose_map(void)
{
    unsigned char *dst = qt_screen_pixels();
    unsigned char *src = qt_map_pixels();
    int dst_stride = qt_screen_stride();
    int src_stride = qt_map_stride();
    int width  = qt_map_width();
    int height = qt_map_height();
    int x = WINDOW_LEFT, y = WINDOW_UP;
    int bpp = qt_screen_bpp();
    int pixel = (bpp + 7) / 8;
    int row, bytes;
    static int complained = 0;

    if (dst == NULL || src == NULL) return;

    /* Both surfaces have to agree on the pixel size, otherwise the rows
     * land at the wrong offsets and the map comes out striped.  Say so
     * rather than painting nonsense. */
    if (bpp != qt_map_bpp() || pixel == 0) {
        if (!complained) {
            complained = 1;
            fprintf(stderr, "screen is %d bpp but the map surface is %d bpp, "
                            "not composing (run with IMAGEQT_DEBUG=1)\n",
                    bpp, qt_map_bpp());
        }
        return;
    }

    /* the map is taller than the screen leaves room for, so clip it */
    if (x + width  > qt_screen_width())  width  = qt_screen_width()  - x;
    if (y + height > qt_screen_height()) height = qt_screen_height() - y;
    if (width <= 0 || height <= 0) return;

    bytes = width * pixel;
    for (row = 0; row < height; row++)
        memcpy(dst + (long)(y + row) * dst_stride + (long)x * pixel,
               src + (long)row * src_stride, bytes);
}

/* Everything the front end needs to know to interpret the surfaces, for
 * when the picture comes out wrong on a machine one cannot look at. */
void qt_dump_surfaces(void)
{
    GrColor red   = GrAllocColor(255, 0, 0);
    GrColor green = GrAllocColor(0, 255, 0);
    GrColor blue  = GrAllocColor(0, 0, 255);

    fprintf(stderr, "screen: %dx%d %d bpp stride %d base %p\n",
            qt_screen_width(), qt_screen_height(), qt_screen_bpp(),
            qt_screen_stride(), (void *)qt_screen_pixels());
    fprintf(stderr, "map   : %dx%d %d bpp stride %d base %p origin %d,%d\n",
            qt_map_width(), qt_map_height(), qt_map_bpp(),
            qt_map_stride(), (void *)qt_map_pixels(),
            qt_map_origin_x(), qt_map_origin_y());
    fprintf(stderr, "colors: red=0x%06lx green=0x%06lx blue=0x%06lx "
                    "(expect ff0000 00ff00 0000ff)\n",
            (unsigned long)red, (unsigned long)green, (unsigned long)blue);
}
