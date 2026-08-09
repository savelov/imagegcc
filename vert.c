#include <grx20.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "image.h"

#define VertLines 4      /* lines per 1 vertical km */
#define MaxBoxSize 16     /* MaxVertKm*VertLines*MaxBoxSize <= DownY-15 !!!!*/
#define MaxVertSize WINDOW_YSIZE/2  /* do not change!!! */
/* pta is indexed by tab[i], a column of the section, and vert_size below can
 * reach WINDOW_XSIZE/2 - so it is the width of the window that bounds it, not
 * MaxVertSize.  On any landscape window the two differ, and pta used to be the
 * short one.  The slack covers the gap scan near the end, which looks up to
 * pta[i+6]. */
#define MaxVertWidth (WINDOW_XSIZE/2+8)
#define MaxVertKm 12
#define MTX vert_size
#define MTY MaxVertKm*VertLines
/*#define MaxHorizLine 540*/  /* in points, 30 reserved */
#define MaxHorizLine (WINDOW_XSIZE-20)
#define LeftX 120
#define Resvd 3  /* for line */
#define ResvdText 17 /* for text */
#define KmNumbers 10
/*#define FlushBuffer()  poke(0x40,0x1a,peek(0x40,0x1c)) */
/*#define DownY 410*/
#define DownY (WINDOW_YSIZE-52)
#define c254 254
#define c256 256
#define Ref  8500

int             run_vert;
extern float    HMRL[MaxPorts];
extern long     colors[NUM_COLORS];
extern float    naa[MaxPorts];
extern int      mapsize;
float           hi,hii;

void vert(int x1,int y1,int x2,int y2) {
int             incx,incy;
int             delta,delta_x,delta_y;
int             countx,county;
int             x,y,vi;
unsigned char   *vert_array,*ptr;
unsigned char   pta[MaxVertWidth];
float           r,delta_r;
int             vert_size;
int             tab[MaxVertSize];
int             line_len;
int             i,j;
//extern int      masx[MaxPorts];
//extern int      masy[MaxPorts];
float           sintet1[MaxPorts],r2,r1;
float           rtt[MaxPorts],rkk;
int             level,ii;
int             box_size;
int             port;
/* ny is only assigned when some port comes out nearer than the 10000 rkk
 * starts at.  A NaN distance makes every comparison false and left it holding
 * whatever was on the stack, which then indexed sintet1[] and HMRL[]. */
int             k,k1,k2,ny=0;
int             nlev=0,kmin=MaxVertKm*VertLines,kmax=-1,filled=0;
const char     *vdbg=getenv("VERT_DEBUG");
unsigned char   flag;
unsigned char   dol,gora;
unsigned char   v_gora,v_dol;
float           fpom,bpom;
char            number[20];


/* These are variable length arrays, so one past the end is not spare padding
 * the way it may be for a fixed array - it is whatever the compiler put next
 * in the frame, and aarch64 does not put the same thing there as x86-64. */
for(i=0; i<MaxVertSize; i++) tab[i]=0;
for(i=0; i<MaxVertWidth; i++) pta[i]=0;
				for(port=0;port<MaxPorts; port++)
				{
				sintet1[port]=sin(naa[port]);
				}


 /*for(i=0; i<=mapsize; i++) printf("\n %i %i",i,HV[0][i]);*/
  run_vert=1;
  delta_x=x2-x1;
	delta_y=y2-y1;


  if (delta_x<0) { incx=-1; delta_x=-delta_x; }
  else if (delta_x>0) incx=1;
  else incx=0;
	printf("\nx1%i,y1%i",x1,y1);
	printf("\nx2%i,y2%i",x2,y2);
  if (delta_y<0) { incy=-1; delta_y=-delta_y; }
  else if (delta_y>0) incy=1;
  else incy=0;

	delta=(delta_x>delta_y)?delta_x:delta_y;
	 printf("\n delta=%i",delta);
   r=sqrt((float)delta_x*delta_x+(float)delta_y*delta_y);
  if (delta) delta_r=r/delta; else delta_r=0;
	for (i=0;i<MaxVertSize;i++) tab[i]=i*delta_r+.5;
	/* tab[delta], which is the length of the cut - but delta counts grid
	 * cells and tab[] is only MaxVertSize long, so a cut wider than the
	 * array read past its end.  It is the same arithmetic without it. */
	line_len=delta*delta_r+.5;
  vert_size=WINDOW_XSIZE/MaxBoxSize;
  for (i=MaxBoxSize;i>2;i--)
   if (line_len>=MaxHorizLine/i)
	 vert_size=WINDOW_XSIZE/(i-1);
	 printf("\n vert_s=%i",vert_size);
	vert_array=malloc(vert_size*MaxVertKm*VertLines+500);
	memset(vert_array,c254,vert_size*MaxVertKm*VertLines);

	 for (i=0,x=x1,y=y1,countx=0,county=0;tab[i]<vert_size && i<MaxVertSize;
			       countx+=delta_x,county+=delta_y,i++) {
       if (countx>=delta) {
	 countx-=delta;
	 x+=incx;
	 if (x>=MSIZE_int) break;
       }
       if (county>=delta) {
	 county-=delta;
	 y+=incy;
	 if (y>=MSIZE_int) break;
			 }
	rkk=10000;
	for(port=0;port<MaxPorts; port++) rtt[port]=0;
		for(port=0;port<MaxPorts; port++)
		{
// printf("\n %i %i",masx[port],masy[port]);
// printf("\n %i %i",masx[1],masy[1]);
// printf("\n %i %i",masx[2],masy[2]);

		 rtt[port]=hypot((double)(x-masx[port]),(double)(y-masy[port]));
		 if((rkk-rtt[port])>=0) {rkk=rtt[port];ny=port;}
		 else rkk=rkk;
		}
		vi=0;
		 rkk=rkk*MRES*2;
//		 printf("\n sin=%6.2f
		 vi=(int)((rkk*(rkk/2/Ref+sintet1[ny])+HMRL[ny])*VertLines);
//	 printf("\n rkk=%4.2f vi=%i", rkk,vi);
			 pta[tab[i]]=vi;
     }

	k=0; hi=0.;
	/* the constant altitude reflectivity levels, wherever they sit in
	 * maps[] - the table is built at start up now, so their indices are
	 * not the fixed 4..11 this loop used to walk */
	for (level=0;level<no_maps;level++)
{
	if (maps[level].family!=FAM_DBZ || maps[level].level==0) continue;
	if (maps[level].mapres==0 || maps[level].bufdata==NULL) continue;
//		 for (port=0;port<MaxPorts;port++)
	if (maps[level].bufhead[0]/10.!=0)
//{	 kk=maps[level].bufhead[0]/10.; break;}
//	 if (port==MaxPorts) continue;   /* no maps found */
		k=maps[level].bufhead[0]/10.*VertLines-0.5;;

/* printf("\n l=%i k=%i",level,k);*/
	 if (k>=MaxVertKm*VertLines) continue;  /* too high ... */
	 /* k is a row of vert_array, taken from the height in the product
	  * header.  Only the top was ever checked, so a header that gives a
	  * negative height aimed ptr before the buffer and the fill below wrote
	  * there. */
	 if (k<0) continue;                     /* ... or below the ground */
	 nlev++; if (k<kmin) kmin=k; if (k>kmax) kmax=k;
	 ptr=vert_array+k*vert_size;
			 for (i=0,x=x1,y=y1,countx=0,county=0;tab[i]<vert_size && i<MaxVertSize;
			       countx+=delta_x,county+=delta_y,i++)
	   {
       if (countx>=delta) {
	 countx-=delta;
	 x+=incx;
	 if (x>=MSIZE_int) break;
			  }
       if (county>=delta) {
	 county-=delta;
	 y+=incy;
	 if (y>=MSIZE_int) break;
			  }
      ptr[tab[i]]=*(maps[level].bufdata+(long)y*MSIZE_int+x);

	   }

      /* horiz. interpolation  */
     for (i=1;i<vert_size-1;i++)
       if (ptr[i]==c254 && ptr[i-1]!=c254 && ptr[i+1]!=c254)
	  ptr[i]=(ptr[i-1]+ptr[i+1])/2;
 }/*  for each level */

/*  #ifndef NOINTER*/
  for (i=0; i<MTX; i++) {
    j=0;
    while ((*(vert_array+i+j*MTX)==c254) && (j<MTY)) j++;
    flag=0;
    do {
      while ((*(vert_array+i+j*MTX)!=c254) && j<MTY) j++;
      dol=j;
      while ((*(vert_array+i+j*MTX)==c254) && j<MTY) j++;
      if (j==MTY) flag=1;
      if (!flag) {
	gora=j;  v_gora=*(vert_array+i+gora*MTX); v_dol=*(vert_array+i+(dol-1)*MTX);
	if (v_gora==v_dol)
	    for (k=dol; k<gora; k++) *(vert_array+i+k*MTX)=v_dol;
	  else {
	  if(!v_gora) fpom= ((float)(-v_dol))/((float)(gora-dol+1))*2.;
	  else        fpom= ((float)(v_gora-v_dol))/((float)(gora-dol+1));
	 bpom=v_dol+fpom;  if (bpom<0.) bpom=0.0;
	  for (k=dol; k<gora; k++) {
	    *(vert_array+i+k*MTX)=(unsigned char) bpom;
	    bpom+=fpom;  if (bpom<0.) bpom=0.0;
	  }
	  } /* else v_gora==v_dol */
	} /* ! flag */
    } while (!flag);
  } /* for i */
/*#endif*/
    box_size=WINDOW_XSIZE/(vert_size);
  //    box_size=(int)hii;
	/*	printf("\n box=%i",box_size);*/

/* VERT_DEBUG=1 says what was computed and where it is about to be drawn.  A
 * section that comes out blank is either empty (no level contributed), the
 * wrong colour, or off the window, and these three lines tell which. */
if (vdbg) {
   for (i=0;i<MTX*MTY;i++) if (vert_array[i]!=c254) filled++;
   printf("\nvert: window %dx%d  vert_size=%d box=%d MTX=%d MTY=%d\n",
          WINDOW_XSIZE,WINDOW_YSIZE,vert_size,box_size,MTX,MTY);
   printf("vert: %d level(s) filled rows %d..%d, %d of %d cells set\n",
          nlev,nlev?kmin:-1,kmax,filled,MTX*MTY);
   printf("vert: boxes x %d..%d y %d..%d  (DownY=%d, screen %dx%d)\n",
          LeftX,LeftX+box_size/2*(MaxHorizLine/box_size*2),
          DownY-(MaxVertKm*VertLines*2-2)*box_size/2,DownY,
          DownY,GrScreenX(),GrScreenY());
   fflush(stdout);
}

 solid_bar(LeftX-30,1,LeftX+MaxHorizLine,WINDOW_YSIZE+20,0);   /* clear screen */

	for(i=0; i<MTX; i++)
			{	ii=i;
			for(j=0; j<MTY; j++)
	{ if(pta[i]==0)   { /*if (pta[i+1]==0) break;*/
											while (pta[ii+1]==0 && (ii-i)<5) ii++;
											if (i==0) pta[i]=pta[ii+1];
											else
											pta[i]=(pta[i-1]+pta[ii+1])/2;
											}
		if(j<pta[i]) *(vert_array+i+j*MTX)=254;
		}}

  /* MTX by MTY, not MTX by MTX: expand() reads a square, and vert_array holds
   * MaxVertKm*VertLines rows of vert_size.  At the sizes a long cut produces
   * that read ran tens of kB past the end of the malloc. */
  expand_rect (vert_array,mapbuffer,MTX,MTY);
	for (level=0;level<MaxVertKm*VertLines*2-2;level++)
	    {
	    ptr=mapbuffer+level*vert_size*2;
	      for (i=0;i<MaxHorizLine/box_size*2;i++)
//	      GrFilledBox(LeftX+box_size*i,DownY-level*box_size,
//	      LeftX+box_size*(i+1)-1,DownY-(level+1)*box_size+1,clev[ptr[i]]);
	       GrFilledBox(LeftX+box_size/2*i,DownY-level*box_size/2,
	      LeftX+box_size/2*(i+1)-1,DownY-(level+1)*box_size/2+1,clev[ptr[i]]);
	    }
// free(mapbuff);
    /* draw coordinates .... */
  outtextxy(LeftX+Resvd,VertLines*MaxVertKm-ResvdText,"km");
  outtextxy(LeftX+MaxHorizLine-15,DownY+ResvdText-5,"km");
  GrLine(LeftX-Resvd,DownY+Resvd,MaxHorizLine+LeftX-1,DownY+Resvd,colors[15]);
  GrLine(LeftX-Resvd,DownY+Resvd-1,MaxHorizLine+LeftX-1,DownY+Resvd-1,colors[15]);
  GrLine(LeftX-Resvd,DownY+Resvd+1,MaxHorizLine+LeftX-1,DownY+Resvd+1,colors[15]);

  GrLine(LeftX-Resvd,DownY+Resvd,LeftX-Resvd,VertLines*MaxVertKm-30,colors[15]);
  GrLine(LeftX-Resvd+1,DownY+Resvd,LeftX-Resvd+1,VertLines*MaxVertKm-30,colors[15]);
  GrLine(LeftX-Resvd-1,DownY+Resvd,LeftX-Resvd-1,VertLines*MaxVertKm-30,colors[15]);

  GrLine(LeftX-Resvd-5,VertLines*MaxVertKm-25,LeftX-Resvd,VertLines*MaxVertKm-35,colors[15]);
    GrLine(LeftX-Resvd-4,VertLines*MaxVertKm-25,LeftX-Resvd+1,VertLines*MaxVertKm-35,colors[15]);
      GrLine(LeftX-Resvd-6,VertLines*MaxVertKm-25,LeftX-Resvd-1,VertLines*MaxVertKm-35,colors[15]);
  GrLine(LeftX-Resvd+5,VertLines*MaxVertKm-25,LeftX-Resvd,VertLines*MaxVertKm-35,colors[15]);
    GrLine(LeftX-Resvd+4,VertLines*MaxVertKm-25,LeftX-Resvd-1,VertLines*MaxVertKm-35,colors[15]);
      GrLine(LeftX-Resvd+6,VertLines*MaxVertKm-25,LeftX-Resvd+1,VertLines*MaxVertKm-35,colors[15]);

  GrLine(LeftX+MaxHorizLine,DownY+Resvd,LeftX+MaxHorizLine-10,DownY+Resvd-5,colors[15]);
    GrLine(LeftX+MaxHorizLine,DownY+Resvd+1,LeftX+MaxHorizLine-10,DownY+Resvd-4,colors[15]);
      GrLine(LeftX+MaxHorizLine,DownY+Resvd-1,LeftX+MaxHorizLine-10,DownY+Resvd-6,colors[15]);
  GrLine(LeftX+MaxHorizLine,DownY+Resvd,LeftX+MaxHorizLine-10,DownY+Resvd+5,colors[15]);
    GrLine(LeftX+MaxHorizLine,DownY+Resvd-1,LeftX+MaxHorizLine-10,DownY+Resvd+4,colors[15]);
      GrLine(LeftX+MaxHorizLine,DownY+Resvd+1,LeftX+MaxHorizLine-10,DownY+Resvd+6,colors[15]);

  for (level=1;level<=MaxVertKm;level++)
     {
     sprintf(number,"%2d",level);
     y=DownY-Resvd-VertLines*box_size*level;
     if (level)
     GrLine(LeftX-Resvd,y+box_size/2,LeftX-Resvd-3,y+box_size/2,colors[15]);
     GrLine(LeftX-Resvd,y+box_size/2+1,LeftX-Resvd-3,y+box_size/2+1,colors[15]);
     GrLine(LeftX-Resvd,y+box_size/2-1,LeftX-Resvd-3,y+box_size/2-1,colors[15]);
     outtextxy(LeftX-ResvdText-6,y,number);
     }

delta=(ceil)((MaxHorizLine*MRES/2/box_size/KmNumbers*2));
  if (delta==0) delta=1;
  for (level=0;level<MaxHorizLine*MRES/box_size/delta;level++)
     {
     sprintf(number,"%d",level*delta*2);
     x=LeftX+level*delta*box_size/MRES;
     GrLine(x,DownY+Resvd,x,DownY+Resvd+3,colors[15]);
     GrLine(x+1,DownY+Resvd,x+1,DownY+Resvd+3,colors[15]);
     GrLine(x-1,DownY+Resvd,x-1,DownY+Resvd+3,colors[15]);
     outtextxy(x-4*strlen(number),DownY+ResvdText-5,number);
     }
  for (level=1;level<=MaxVertKm;level++)
      {
      y=DownY-Resvd-VertLines*box_size*level;
      if (level)
      GrLine(LeftX,y+box_size/2,LeftX+MaxHorizLine-1,y+box_size/2,colors[0]);
      }
  for (level=1;level<MaxHorizLine*MRES/box_size/delta;level++)
      {
     x=LeftX+level*delta*box_size/MRES;
     GrLine(x,1,x,DownY,colors[0]);
      }
    /* x=LeftX+box_size/2+r*box_size;*/
    x=LeftX+r*box_size;
     GrLine(x,DownY,x,DownY+10,colors[14]);
GrBox(WINDOW_LEFT-2,WINDOW_UP,WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE+2,GrAllocColor(255,255,255));
 free(vert_array);
	       run_vert=0;
   flagv=1;
    }

