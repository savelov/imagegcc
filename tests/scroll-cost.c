/* scroll-cost - where a scroll step's time actually goes.
 *
 * One cursor key in imagegcc moves the map 5 pixels and calls draw_map(),
 * which does the same three things on every platform:
 *
 *   1. draw the map scanlines into a RAM context   (memory writes)
 *   2. GrBitBlt that RAM context onto the screen   (one big transfer)
 *   3. redraw the legend, the frame and the header on the screen
 *      (many small primitives)
 *
 * Steps 2 and 3 are the ones whose cost depends on the video driver, so
 * time them separately.  Run the same binary against the X11 driver and
 * against the memory driver (which is what the Win32 GDI driver behaves
 * like: a linear frame buffer in process memory) and compare.
 *
 * Usage: scroll-cost [driver]     default: whatever GRX picks
 *
 * A benchmark, not a pass/fail check, so it is deliberately not in the
 * TESTS list in run-tests.sh - there is no right answer for it to assert.
 * Build it by hand:
 *   gcc -O2 -o scroll-cost scroll-cost.c -I $GRX/include \
 *       $GRX/lib/unix/libgrx20X.a -lX11 -lz -lpng -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <grx20.h>

#define SW 1920          /* what showmap.c asks for in the GUI build */
#define SH 1080
#define MW 1300          /* a representative map window */
#define MH  900
#define REPS  20

/* X requests are buffered, so a phase can be billed to whatever runs next.
 * One pixel read back forces the server to catch up; it costs the same in
 * every phase, so it does not distort the comparison. */
static void sync_screen(void) { (void)GrPixel(0, 0); }

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/* the map draw: one horizontal run per scanline, straight into RAM */
static void fill_ram(GrContext *ram)
{
    GrContext saved;
    int y;
    GrSaveContext(&saved);
    GrSetContext(ram);
    for (y = 0; y < MH; y++)
        GrHLine(0, MW - 1, y, GrAllocColor(y & 0xff, 0x40, 0x80));
    GrSetContext(&saved);
}

/* draw_legend()+show_header(): a column of colour swatches with a label
 * beside each, plus the frame round the map.  Small primitives, on screen.
 * "what" selects the halves: 1 boxes, 2 text, 3 both. */
static void draw_furniture_part(int what)
{
    int i;
    GrTextOption o;
    memset(&o, 0, sizeof o);
    o.txo_font = &GrDefaultFont;
    o.txo_xalign = GR_ALIGN_LEFT;
    o.txo_yalign = GR_ALIGN_TOP;
    o.txo_direct = GR_TEXT_RIGHT;
    o.txo_fgcolor.v = GrWhite();
    o.txo_bgcolor.v = GrBlack();

    for (i = 0; i < 16; i++) {
        if (what & 1)
            GrFilledBox(10, 40 + i * 30, 60, 40 + i * 30 + 25,
                        GrAllocColor(i * 16, 255 - i * 16, 128));
        if (what & 2)
            GrDrawString("-99.9", 5, 70, 42 + i * 30, &o);
    }
    if (what & 1) GrBox(99, 39, 100 + MW, 40 + MH, GrWhite());
    if (what & 2) GrDrawString("header line 12:34 01.01.26", 26, 110, 8, &o);
}

static void draw_furniture(void) { draw_furniture_part(3); }

int main(int argc, char **argv)
{
    const char *drv = (argc > 1) ? argv[1] : NULL;
    GrContext *ram;
    double t0, t_ram, t_blt, t_furn, t_box, t_text, t_read;
    int i;

    if (drv && !GrSetDriver((char *)drv)) {
        printf("scroll-cost: no driver \"%s\"\n", drv);
        return 1;
    }
    if (!GrSetMode(GR_width_height_color_graphics, SW, SH, 256 * 256 * 256L)) {
        printf("scroll-cost: cannot set %dx%d truecolor\n", SW, SH);
        return 1;
    }
    printf("driver=%-8s %dx%d bpp=%d  core frame mode=%d\n",
           drv ? drv : "(default)", GrScreenX(), GrScreenY(),
           GrScreenContext()->gc_driver->bits_per_pixel, (int)GrCoreFrameMode());

    ram = GrCreateFrameContext(GrCoreFrameMode(), MW, MH, NULL, NULL);
    if (!ram)   /* the memory driver names no compatible RAM mode */
        ram = GrCreateFrameContext(GR_frameRAM24, MW, MH, NULL, NULL);
    if (!ram) { printf("scroll-cost: no RAM context\n"); return 1; }

    /* 1. the map itself, into RAM */
    t0 = now_ms();
    for (i = 0; i < REPS; i++) fill_ram(ram);
    sync_screen();
    t_ram = (now_ms() - t0) / REPS;

    /* 2. the RAM -> screen transfer */
    t0 = now_ms();
    for (i = 0; i < REPS; i++)
        GrBitBlt(NULL, 100, 40, ram, 0, 0, MW - 1, MH - 1, GrWRITE);
    sync_screen();
    t_blt = (now_ms() - t0) / REPS;

    /* 3. the legend / frame / header, on the screen context */
    t0 = now_ms();
    for (i = 0; i < REPS; i++) draw_furniture();
    sync_screen();
    t_furn = (now_ms() - t0) / REPS;

    t0 = now_ms();
    for (i = 0; i < REPS; i++) draw_furniture_part(1);
    sync_screen();
    t_box = (now_ms() - t0) / REPS;
    t0 = now_ms();
    for (i = 0; i < REPS; i++) draw_furniture_part(2);
    sync_screen();
    t_text = (now_ms() - t0) / REPS;

    /* 4. a single pixel read back from the screen - free from a linear
     *    frame buffer, a synchronous server round trip over X11.  GRX
     *    caches 4 rows, so step down the screen to defeat the cache. */
    t0 = now_ms();
    for (i = 0; i < 200; i++) (void)GrPixel(50, (i * 37) % SH);
    t_read = (now_ms() - t0) / 200;

    printf("  map into RAM        %8.2f ms\n", t_ram);
    printf("  GrBitBlt to screen  %8.2f ms\n", t_blt);
    printf("  legend+frame+header %8.2f ms   (boxes %.2f, text %.2f)\n",
           t_furn, t_box, t_text);
    printf("  ---------------------------------\n");
    printf("  one scroll step     %8.2f ms  -> %.1f steps/s\n",
           t_ram + t_blt + t_furn, 1000.0 / (t_ram + t_blt + t_furn));
    printf("  GrPixel read back   %8.3f ms each\n", t_read);

    /* 5. the same scroll step with the mouse cursor showing.  GRX draws a
     *    software cursor, so every primitive that lands under it makes the
     *    driver save and restore the pixels beneath - see step 4. */
    if (GrMouseDetect()) {
        GrMouseInit();
        GrMouseSetCursorMode(GR_M_CUR_NORMAL);
        GrMouseWarp(100 + MW / 2, 40 + MH / 2);   /* park it over the map */
        GrMouseDisplayCursor();
        t0 = now_ms();
        for (i = 0; i < REPS; i++) {
            fill_ram(ram);
            GrBitBlt(NULL, 100, 40, ram, 0, 0, MW - 1, MH - 1, GrWRITE);
            draw_furniture();
        }
        sync_screen();
        printf("  same step, cursor on%8.2f ms\n", (now_ms() - t0) / REPS);
        GrMouseUnInit();
    }

    GrSetMode(GR_default_text);
    return 0;
}
