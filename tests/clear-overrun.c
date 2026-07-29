#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <grx20.h>
static unsigned char buf[4000000];
int main(int argc, char **argv)
{
    int w = atoi(argv[1]), h = atoi(argv[2]);
    char *mem[4]; GrContext ctx; long i, first = -1;
    memset(buf, 0xAA, sizeof buf);
    mem[0] = (char*)buf; mem[1]=mem[2]=mem[3]=NULL;
    if (!GrCreateFrameContext(GR_frameRAM8,w,h,mem,&ctx)) { printf("ctxfail\n"); return 1; }
    GrSetContext(&ctx);
    GrClearContext(GrBlack());
    for (i=(long)w*h; i<(long)w*h+8192 && i<(long)sizeof buf; i++)
        if (buf[i]!=0xAA) { first=i-(long)w*h; break; }
    printf("w=%-5d h=%-5d lineoffset=%-5d -> %s\n", w, h, ctx.gc_lineoffset,
           first<0 ? "clean" : "OVERRUN");
    return 0;
}
