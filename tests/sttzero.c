#include <stdio.h>
#include <string.h>
#include <grx20.h>
#include "libgrx.h"
#include "memfill.h"
int main(void){
  GrContext c; unsigned char *p=(unsigned char*)&c; size_t i,nz=0;
  memset(&c,0xAA,sizeof c);
  sttzero(&c);
  for(i=0;i<sizeof c;i++) if(p[i]) nz++;
  printf("sizeof(GrContext)=%d  bytes still non-zero after sttzero: %d\n",(int)sizeof c,(int)nz);
  printf("gc_xoffset=%d gc_yoffset=%d\n", c.gc_xoffset, c.gc_yoffset);
  return 0;
}
