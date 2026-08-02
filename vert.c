#include <grx20.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "image.h"

#define VertLines 4      /* lines per 1 vertical km */
#define MaxBoxSize 16     /* MaxVertKm*VertLines*MaxBoxSize <= DownY-15 !!!!*/
#define MaxVertSize WINDOW_YSIZE/2  /* do not change!!! */
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
unsigned char   pta[MaxVertSize];
float           r,delta_r;
int             vert_size;
unsigned char   table[MaxVertSize];
int             tab[MaxVertSize];
int             i,j;
//extern int      masx[MaxPorts];
//extern int      masy[MaxPorts];
float           sintet1[MaxPorts],r2,r1;
float           rtt[MaxPorts],rkk;
int             level,ii;
int             box_size;
int             port;
int             k,k1,k2,ny;
unsigned char   flag;
unsigned char   dol,gora;
unsigned char   v_gora,v_dol;
float           fpom,bpom;
char            number[20];


for(i=0; i<=MaxVertSize; i++) {table[i]=0; tab[i]=0; pta[i]=0;}
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
	for (i=0;i<MaxVertSize;i++) {table[i]=i*delta_r+.5;tab[i]=i*delta_r+.5;}
	/*	 printf("\n tab=%i",table[delta]);*/
  vert_size=WINDOW_XSIZE/MaxBoxSize;
  for (i=MaxBoxSize;i>2;i--)
   if (table[delta]>=MaxHorizLine/i)
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
	 ptr=vert_array+k*vert_size;
			 for (i=0,x=x1,y=y1,countx=0,county=0;table[i]<vert_size && i<MaxVertSize;
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
      ptr[table[i]]=*(maps[level].bufdata+(long)y*MSIZE_int+x);

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

  expand (vert_array,mapbuffer,vert_size);
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

