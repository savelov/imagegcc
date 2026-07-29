#include <stdio.h>
#include <grx20.h>
static void run(int w, GrColor c, const char *tag)
{
    GrContext *s = GrScreenContext();
    unsigned char *p; int i, bad = 0;
    GrFilledBox(100, 50, 100 + w - 1, 50, GrAllocColor(0,0,0));   /* clear */
    GrFilledBox(100, 50, 100 + w - 1, 50, c);
    p = (unsigned char *)s->gc_baseaddr[0] + (long)50 * s->gc_lineoffset + 100*3;
    for (i = 0; i < w*3; i++) if (p[i] == 0) bad++;
    printf("  w=%d %-9s bytes:", w, tag);
    for (i = 0; i < w*3 && i < 12; i++) printf(" %02x", p[i]);
    printf("   %s\n", bad ? "<-- ZEROS" : "ok");
}
int main(void)
{
    GrColor eq, ne;
    GrSetDriver("memory");
    GrSetMode(GR_width_height_color_graphics,640,480,256*256*256L);
    eq = GrAllocColor(192,192,192);   /* equal bytes -> fast path  */
    ne = GrAllocColor(0,128,192);     /* unequal     -> general    */
    for (int w = 1; w <= 4; w++) run(w, eq, "c0c0c0");
    { int ws[4]={8,16,63,100}; for (int i=0;i<4;i++) run(ws[i], eq, "c0c0c0"); }
    for (int w = 1; w <= 4; w++) run(w, ne, "0080c0");
    return 0;
}
