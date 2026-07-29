#include <stdio.h>
#include <grx20.h>
int main(void)
{
    int i;
    static const int v[][3] = {{0,0,192},{0,0,128},{0,0,255},{0,128,192},{192,0,0},{0,192,0}};
    if (!GrSetDriver("memory")) { printf("no memory driver\n"); return 1; }
    if (!GrSetMode(GR_width_height_color_graphics,640,480,256*256*256L)) { printf("no mode\n"); return 1; }
    printf("ncolors=%ld\n", (long)GrNumColors());
    for (i = 0; i < 6; i++)
        printf("  GrAllocColor(%3d,%3d,%3d) = 0x%06lx\n", v[i][0], v[i][1], v[i][2],
               (unsigned long)GrAllocColor(v[i][0], v[i][1], v[i][2]));
    return 0;
}
