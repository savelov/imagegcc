#include <stdio.h>
#include <grx20.h>
static void probe(GrColor c, const char *name)
{
    GrContext *s = GrScreenContext();
    unsigned char *p;
    GrFilledBox(10, 10, 40, 40, c);
    p = (unsigned char *)s->gc_baseaddr[0] + (long)20 * s->gc_lineoffset + 20*3;
    printf("  %-10s color=0x%06lx -> bytes %02x %02x %02x   readpixel=0x%06lx\n",
           name, (unsigned long)c, p[0], p[1], p[2],
           (unsigned long)GrPixel(20,20));
}
int main(void)
{
    if (!GrSetDriver("memory")) { printf("no driver\n"); return 1; }
    if (!GrSetMode(GR_width_height_color_graphics,640,480,256*256*256L)) { printf("no mode\n"); return 1; }
    probe(GrAllocColor(0,0,192),  "blue192");
    probe(GrAllocColor(0,0,128),  "blue128");
    probe(GrAllocColor(0,0,255),  "blue255");
    probe(GrAllocColor(0,128,192),"grn+blue");
    probe(GrAllocColor(192,0,0),  "red192");
    probe(GrAllocColor(192,192,192),"grey c0c0c0");
    probe(GrAllocColor(128,128,128),"grey 808080");
    probe(GrAllocColor(64,64,64),  "grey 404040");
    return 0;
}
