#include <stdio.h>
#include <string.h>
#include <grx20.h>
static unsigned char buf[256*64];
int main(void)
{
    char *mem[4]; GrContext ctx; int w, i, filled;
    mem[0]=(char*)buf; mem[1]=mem[2]=mem[3]=NULL;
    GrCreateFrameContext(GR_frameRAM8,256,64,mem,&ctx);
    GrSetContext(&ctx);
    for (w = 1; w <= 8; w++) {
        memset(buf, 0, sizeof buf);
        GrFilledBox(0, 10, w-1, 10, (GrColor)5);
        filled = 0;
        for (i = 0; i < 16; i++) if (buf[10*256+i]) filled++;
        printf("  w=%d -> %d bytes filled %s\n", w, filled, filled==w ? "ok" : "<-- WRONG");
    }
    return 0;
}
