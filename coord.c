#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
#ifdef __TURBOC__
# include <malloc.h>    /* for farmalloc; stdlib.h above covers malloc,
                           and darwin has no <malloc.h> at all */
#endif
#include "proj_compat.h"

/* The packed coordinate tables put a byte count in front of each short, so
 * every short in them sits at an odd address.  Reading one through a short *
 * is undefined - x86 does not care and aarch64 mostly does not either, but
 * the compiler is entitled to assume the alignment it was promised, and
 * -fsanitize=undefined reported every one of these.  memcpy says what is
 * meant and compiles to the same instruction. */
static int get_short(const unsigned char *p)
{
   short v;
   memcpy(&v,p,sizeof v);
   return v;
}

static void put_short(unsigned char *p,int v)
{
   short s=(short)v;
   memcpy(p,&s,sizeof s);
}

#define MaxPortTables MaxPorts

//TODO - calculate center and fix 54/100 lat/lon

char krass_projection[]= "+proj=eqdc +lat_1=47 +lat_2=62 +lat_0=%f +lon_0=%f +x_0=0 +y_0=0";

unsigned char *table[MaxPortTables];
float L[MaxPortTables],B[MaxPortTables];
int table_size[MaxPortTables];
float Res[MaxPortTables];
int MapSize[MaxPortTables];
int last_port;

float a=6378.245,ex2=6.6934e-3;


void convert_port(float LA,float BA,float X,float Y,float *Lar,float *Fir) {
            projPJ pj_merc, pj_latlong;
            double x, y;
	    int p;
	    char string[128];

             sprintf(string,"+proj=aeqd +lat_0=%f +lon_0=%f +k_0=1.0 +x_0=0 +y_0=0",BA/DEG_TO_RAD,LA/DEG_TO_RAD );

            if (!(pj_merc = pj_init_plus(string)) )
               exit(1);
            if (!(pj_latlong = pj_init_plus("+proj=longlat" )) )
               exit(1);
               x = X*1000.;
               y = Y*1000.;
               p = pj_transform(pj_merc, pj_latlong, 1, 1, &x, &y, NULL );
               *Lar= x;
               *Fir= y;

  pj_free(pj_merc);
  pj_free(pj_latlong);

}

void convert(float LA,float BA,float X,float Y,float *Lar,float *Fir) {
            projPJ pj_merc, pj_latlong;
            double x, y;
	    int p;
	    char string[128];

	    sprintf(string,krass_projection,BA/DEG_TO_RAD,LA/DEG_TO_RAD);

            if (!(pj_merc = pj_init_plus(string)) )
               exit(1);
            if (!(pj_latlong = pj_init_plus("+proj=longlat" )) )
               exit(1);
               x = X*1000.;
               y = Y*1000.;
               p = pj_transform(pj_merc, pj_latlong, 1, 1, &x, &y, NULL );
               *Lar= x;
               *Fir= y;

  pj_free(pj_merc);
  pj_free(pj_latlong);

}

void conv_back(float LA,float BA,float Lar,float Fir,float *X,float *Y) {

            projPJ pj_merc, pj_latlong;
            double x, y;
	    int p;
	    char string[128];

	    sprintf(string,krass_projection,BA/DEG_TO_RAD,LA/DEG_TO_RAD);

            if (!(pj_merc = pj_init_plus(string)) )
               exit(1);
            if (!(pj_latlong = pj_init_plus("+proj=longlat" )) )
               exit(1);
               x = Lar;
               y = Fir;
               p = pj_transform(pj_latlong, pj_merc, 1, 1, &x, &y, NULL );
	       *X = x/1000.;
	       *Y = y/1000.;

  pj_free(pj_merc);
  pj_free(pj_latlong);

}


void check_table(short _HUGE *table,unsigned char *packed_table,unsigned int size_from) {
unsigned int i,j;
short x,y,countx=0,county=0;
unsigned char *in_ptr=packed_table;
short _HUGE *my_table=table;
unsigned char value;

  for (j=0;j<size_from;j++) for (i=0; i<size_from;i++) {

	 if (!countx) {
		countx=*in_ptr++;
		x=get_short(in_ptr);
	        in_ptr+=sizeof(short);
	 }
	 if (!county) {
		county=*in_ptr++;
		y=get_short(in_ptr);
	        in_ptr+=sizeof(short);
	 }

	 if (my_table[(long)j*2*size_from+i*2]  !=x ||
	my_table[(long)j*2*size_from+i*2+1]!=y)   { printf("Internal error! i=%d,j=%d\n",i,j); exit(1); }

	 x++;
	 countx--;
	 county--;
  }

}


/* returns length of packed table */
unsigned short pack_table(short _HUGE *table,unsigned char *packed_table,unsigned int size_from) {
unsigned short x,y,save_x,count_y;
unsigned int i;
short _HUGE *in_ptr=table;
unsigned char *out_ptr=packed_table,*save_xptr,*save_yptr;

  save_x=*in_ptr++;
  x=save_x+1;
  y=*in_ptr++;
  count_y=0;
  save_xptr=out_ptr++;
    out_ptr+=sizeof(short);
  save_yptr=out_ptr++; 
    out_ptr+=sizeof(short);

  for(i=1;i<size_from*size_from;i++) {
		if (*in_ptr!=x++ || (x-save_x-1)>254) {    /* make sure, but really never >size_from */
	 *save_xptr++=x-save_x-1;
	 put_short(save_xptr,save_x);
	 save_x=*in_ptr;
	 x=save_x+1;
	 save_xptr=out_ptr++; out_ptr+=sizeof(short);
		}
		in_ptr++;

		count_y++;
		if (*in_ptr!=y || count_y>254) {
	 *save_yptr++=count_y;
	 put_short(save_yptr,y);
	 y=*in_ptr;
	 count_y=0;
	 save_yptr=out_ptr++; out_ptr+=sizeof(short);
		}
		in_ptr++;
  }
  *save_xptr++=x-save_x;
  put_short(save_xptr,save_x);
  *save_yptr++=count_y+1;
  put_short(save_yptr,y);

  check_table(table,packed_table,size_from);

//  return out_ptr-packed_table;
  return (char *)out_ptr-(char *)packed_table;
}

#define MaxPacked 300000   /* size of 1 packed table */

unsigned char  *get_ptr(float LN,float BN,float MapRes,int size_from,int size_to) {
int port,in,jn,x,y;
int size;
float x1,y1,xn,yn;
float cosf0u,sinf0u,RZ0u;
float cosfi,sinfi,sindl,cosdl,RZI;
float delts2,sinald,cosald,sinf,cosf,chi,obz,sinL,cosL,sinLL0,cosLL0;
int xc=-MapRes*(int)size_to/2,yc=MapRes*(int)size_to/2;
short _HUGE *my_table;
unsigned char *my_packtable;

projPJ pj_stere, pj_merc, pj_latlong;
char port_config[128];
char center_config[128];
double myx, myy;
int p;



for (port=0;port<MaxPortTables;port++)
  if (B[port]==BN && L[port]==LN && Res[port]==MapRes && MapSize[port]==size_from)
		 return table[port];

/* if table not fount, make it */
  port=last_port%MaxPortTables;
  last_port=(last_port+1)%MaxPortTables;

  my_table=_MALLOC((long)size_from*size_from*2*sizeof(short));
  my_packtable=malloc(MaxPacked);

  if (table[port]!=NULL) free(table[port]);

  B[port]=BN; L[port]=LN; Res[port]=MapRes; MapSize[port]=size_from;

//  cosf0u=cos(BU);
// sinf0u=sin(BU);
//  RZ0u=a*sqrt(1.-ex2)/(1-ex2*sinf0u*sinf0u);
//  cosfi=cos(BN);
//  sinfi=sin(BN);
//  cosdl=cos(LN-LU);
//  sindl=sin(LN-LU);
//  RZI=a*sqrt(1.-ex2)/(1-ex2*sinfi*sinfi);


  sprintf(center_config,krass_projection,BU/DEG_TO_RAD,LU/DEG_TO_RAD);
  sprintf(port_config,"+proj=aeqd +lat_0=%f +lon_0=%f +k_0=1.0 +x_0=0 +y_0=0",BN/DEG_TO_RAD,LN/DEG_TO_RAD );

  if (!(pj_stere = pj_init_plus(center_config)) )
   exit(1);

    if (!(pj_latlong = pj_init_plus("+proj=longlat" )) )
   exit(1);

  if (!(pj_merc = pj_init_plus(port_config)) )
   exit(1);


  for (jn=0;jn<size_from;jn++)
		for (in=0;in<size_from;in++) {

		 xn=MapRes*(in-size_from/2)+(float)MapRes/2;   /* +MapRes/2 �.�. �� 0,0 ᮮ�. 2km,2km */
       yn=MapRes*(jn-size_from/2)+(float)MapRes/2;


        myx = xn*1000;
	myy = yn*1000;
        p = pj_transform(pj_merc, pj_latlong, 1, 1, &myx, &myy, NULL );
        p = pj_transform(pj_latlong, pj_stere, 1, 1, &myx, &myy, NULL );
	x1 = myx/1000.;
	y1 = myy/1000.;


//       delts2= (xn*xn + yn*yn)/RZI/RZI;
//       sinald=-xn/RZI;
//       cosald= yn/RZI;
//       sinf  = (sinfi + cosfi*cosald)/(float)sqrt(1. + delts2);
//       cosf  = sqrt( 1. - sinf*sinf );
//       chi   = cosfi - sinfi*cosald;
//       obz   = 1./sqrt( chi*chi + sinald*sinald );
//       sinL  = sinald*obz;
//       cosL  = chi*obz;
//       sinLL0= sindl*cosL-cosdl*sinL;
//       cosLL0= cosdl*cosL+sindl*sinL;
//       obz   = 2.*RZ0u/(1. + sinf*sinf0u+cosf*cosf0u*cosLL0);
//       x1    = cosf*sinLL0*obz;
//       y1    = (cosf0u*sinf - sinf0u*cosf*cosLL0)*obz;

       x=(x1-xc)/MapRes;              /* ����뢠�� �஡��� ���� */
       y=(yc-y1)/MapRes;              /* �.�. 3km,3km ᮮ�. ��.0,0 */

		 if (x<0 || y<0 || x>=size_to || y>=size_to) y=-1;    /* leave x as is */
       my_table[(long)jn*2*size_from+in*2]  =x;
       my_table[(long)jn*2*size_from+in*2+1]=y;

  }
  pj_free(pj_merc);
  pj_free(pj_latlong);
  pj_free(pj_stere);


  size=pack_table(my_table,my_packtable,size_from);
  if (size>MaxPacked) {
    printf("Can't pack the table (size>MaxPackTable)!\n");
    exit(1);
  }
  table_size[port]=size;
  table[port]=malloc(size);
  memcpy(table[port],my_packtable,size);

  free(my_table);
  free(my_packtable);
  return table[port];

}
 void koord(unsigned char *tb,unsigned char *tbl_ptr,unsigned int size_from,int port) {
short x,y,countx=0,county=0;
unsigned char *tb_ptr=tb;
unsigned char *in=tbl_ptr;
unsigned int xtab;   /* in 100 x 100 */
unsigned int ytab;
//extern int masx[MaxPorts];
//extern int masy[MaxPorts];
//printf("\n privet1");
		 for (ytab=0;ytab<size_from; ytab++)
		 for (xtab=0;xtab<size_from; xtab++) {
    if (!countx) {
      countx=*in++;
      x=get_short(in); in+=sizeof(short);
    }
    if (!county) {
      county=*in++;
      y=get_short(in); in+=sizeof(short);
    }
		if((xtab==49)&&(ytab==49)) {masx[port]=x; masy[port]=y; }
		tb_ptr++;
    x++;
    countx--;
    county--;
	}


// printf("\n %i %i",masx[0],masy[0]);
// printf("\n %i %i",masx[1],masy[1]);
// printf("\n %i %i",masx[2],masy[2]);

}
/* The stronger reading wins.  Right for anything whose byte scale is ordered
 * by severity - echo tops, and the phenomena codes 4_myavl.wrk carries. */
void walk_table_max(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,int nodata) {
unsigned int i;
short x,y,countx=0,county=0;
unsigned char *in_ptr=tbl_ptr,*tb_ptr=tb;
unsigned char value;

  for (i=0;i<size_from*size_from;i++) {
	 if (!countx) {
		countx=*in_ptr++;
		x=get_short(in_ptr);
	        in_ptr+=sizeof(short);
	 }
	 if (!county) {
		county=*in_ptr++;
		y=get_short(in_ptr);
	        in_ptr+=sizeof(short);
	 }
	 if (*tb_ptr!=nodata && x!=-1 && y!=-1) {
		value=ntb[y*size_to+x];
		if (value==nodata || value<*tb_ptr) ntb[y*size_to+x]=*tb_ptr;
	 }
	 tb_ptr++;
	 x++;
	 countx--;
	 county--;
  }

}

/* The nearer radar wins.  Averaging a Doppler velocity, or a differential
 * reflectivity, over two radars looking at a cell from different directions
 * produces a number that means nothing, so pick one reading instead of
 * blending them.
 *
 * Which one is decided without remembering who wrote the cell: the port loop
 * in read_files() runs downwards, so anything already in the cell came from a
 * radar with a higher index, and rk below is the distance to the nearest of
 * those.  This sample is the closer one exactly when its own range is
 * shorter. */
void walk_table_near(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,
							  float MapRes, int port,int z,int nodata) {
short x,y,countx=0,county=0;
unsigned char *in=tbl_ptr;
unsigned int xtab,ytab;   /* in 100 x 100 */
int rx,ry,k;
double rt,rk,r;

 for (ytab=0;ytab<size_from; ytab++)
	 for (xtab=0;xtab<size_from; xtab++) {
    if (!countx) {
	countx=*in++;
	x=get_short(in);
	in+=sizeof(short);
    }
    if (!county) {
	county=*in++;
	y=get_short(in);
	in+=sizeof(short);
    }
    if (*tb!=nodata && x!=-1 && y!=-1) {
       if (ntb[(long)y*size_to+x]==nodata)
	  ntb[(long)y*size_to+x]=*tb;
       else {
	  ry=ytab-size_from/2; rx=xtab-size_from/2;
	  rt=hypot((double)rx,(double)ry)*MapRes;   /* range from this radar */
	  rk=1000000.;
	  for (k=port+1; k<=MaxPorts-1; k++) {
	     if (fk[z][k]==0) continue;
	     r=hypot((double)(x-masx[k]),(double)(y-masy[k]))*MapRes;
	     if (r<rk) rk=r;
	  }
	  if (rt<rk) ntb[(long)y*size_to+x]=*tb;
       }
    }
    tb++;
    x++;
    countx--;
    county--;
  }

}

/* Inverse distance weighted in Z, for reflectivity and what is derived from
 * it: two radars seeing the same cell are averaged, weighted by range. */
void walk_table_idw(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,
							  float MapRes, int port,int z,int nodata) {
short i,x,y,countx=0,county=0;
//unsigned char *tb_ptr=tb;
unsigned char *in=tbl_ptr;
float val,jj;
int l;
float v,v0;
float t[254];
unsigned int xtab;   /* in 100 x 100 */
unsigned int ytab;
int rx,ry,k;
double r[MaxPorts],rt,rmin,vt,rk;
 for (ytab=0;ytab<size_from; ytab++)
	 for (xtab=0;xtab<size_from; xtab++) {
    if (!countx) {
      //	countx=*tbl_ptr++;
      //	x=*((short *)tbl_ptr)++;
     	countx=*in++;
	x=get_short(in);
	in+=sizeof(short);

		 }
    if (!county) {
       //	county=*tbl_ptr++;
       //	y=*((short *)tbl_ptr)++;
        county=*in++;
	y=get_short(in);
	in+=sizeof(short);

		 }
     ry=ytab-size_from/2; rx=xtab-size_from/2;
    	rt=hypot((double)rx,(double)ry)*MapRes;
	l=rt;
if ((*tb==nodata && l<Rsv && x!=-1 && y!=-1)||(*tb!=nodata && x!=-1 && y!=-1)) {
	for(i=0; i<MaxPorts; i++) r[i]=0.;
	 val=*(ntb+(long)y*size_to+x);
	if(val==nodata)
	*(ntb+(long)y*size_to+x)=*tb;
	else {
	rk=1000000.;
 	v0=pow(10.,(double)val/30.);
 	 jj=*tb;
        	for(k=port+1; k<=MaxPorts-1; k++)
 	{
         if(fk[z][k]==0) continue;
 	 r[k]=hypot((double)(x-masx[k]),(double)(y-masy[k]))*MapRes;
 	 rk=(rk-r[k])>=0 ? r[k] : rk;
 	}
	 i=rk;
 	 if(jj==nodata)  {
 	if(i>Rst)   *(ntb+(long)y*size_to+x)=nodata;
 	if(i<Rst)    *(ntb+(long)y*size_to+x)=val;
 						 }
  else {
 	 vt=pow(10.,(double)jj/30.);
 	rmin=pow(rk,(double)cof); if(rmin==0.0) rmin=0.1;
 	rt=pow(rt,(double)cof);     if(rt==0.0)   rt=0.1;
      v=((v0/rmin+vt/rt)/(1./rmin+1./rt));
      v=log10(v)*30.;
 	 *(ntb+(long)y*size_to+x)=(char)v;
 	 }     }
     }
 	tb++;

    x++;
    countx--;
    county--;
  }
}

/* Fill a hole whose four neighbours all have readings.  "Hole" is the
 * product's own no-data byte, which is not 254 for every product. */
/* Fill the single cell holes the reprojection leaves behind, from the four
 * neighbours - but only from the ones that actually measured something.
 *
 * "No echo" is an absence, not a reading, and averaging it in invented data.
 * It shows worst in differential reflectivity, whose byte scale is not
 * monotonic: 123 and above mean -0.4..3.5 dB, 81..120 mean -4.6..-0.7, and
 * 80 and below are the high readings, 3.6..8.1 dB.  Mixing the no-echo byte
 * 0 into three ordinary readings of 132 (0.5 dB) gives (132*3+0+2)/4 = 99,
 * which reads back as -2.8 dB - a scatter of blue cells through otherwise
 * quiet weather.
 *
 * `smooth` is 0 for a product whose bytes are codes rather than quantities.
 * Averaging those invents phenomena that were never observed - halfway
 * between a heavy shower and light rain is not a thing - so such a hole takes
 * the commonest of its neighbours instead, ties going to the stronger code,
 * which is how these maps already combine across radars.
 *
 * `wrapped` says the bytes are a quantity but not a monotonic one, which is
 * differential reflectivity and nothing else.  The mean of two ZDR bytes is
 * not the byte of their mean: a hole between 162 (3.5 dB) and 35 (3.6 dB)
 * averages to 98, which is -2.9 dB, a cold cell in the middle of a warm one.
 * Such a hole is filled in the value and encoded back. */
void interpolation (unsigned char *ntb,int map_size,int nodata,int noecho,int smooth,
                    int wrapped) {
int i,j,k,n,sum;
unsigned char *Ptr;
int v[4];
double value;

 for (i=1;i<map_size-1;i++)
	for (j=1; j<map_size-1;j++) {
	  Ptr=ntb+(long)i*map_size+j;
	  if (*Ptr!=nodata) continue;
	  v[0]=*(Ptr-1); v[1]=*(Ptr+1); v[2]=*(Ptr-map_size); v[3]=*(Ptr+map_size);
	  for (k=0;k<4;k++) if (v[k]==nodata) break;
	  if (k<4) continue;                /* a neighbour is a hole as well */
	  if (!smooth) {                    /* codes: the commonest neighbour */
	     int best=v[0],bestn=0,m,c;
	     for (k=0;k<4;k++) {
	        for (c=0,m=0;m<4;m++) if (v[m]==v[k]) c++;
	        if (c>bestn || (c==bestn && v[k]>best)) { best=v[k]; bestn=c; }
	     }
	     *Ptr=best;
	     continue;
	  }
	  sum=n=0; value=0.0;
	  for (k=0;k<4;k++) {
	     if (noecho>=0 && v[k]==noecho) continue;
	     if (wrapped) value+=zdr_value(v[k]); else sum+=v[k];
	     n++;
	  }
	  /* nothing around it saw anything, so neither did this cell */
	  if (n==0) { if (noecho>=0) *Ptr=noecho; continue; }
	  *Ptr= wrapped ? zdr_byte((float)(value/n)) : (sum+n/2)/n;
 }
}

/* Double a w by h array into a 2w by 2h one.  expand() below is this with a
 * square array, which is what a product grid is - the vertical section is not,
 * and calling expand() for it read h*h bytes out of a buffer holding w*h. */
void expand_rect(unsigned char _HUGE *in,unsigned char _HUGE *out,int w,int h) {
int i,j;
unsigned char _HUGE *inptr;
unsigned char _HUGE *outptr;
int mapsize2=w*2;
//int k;
//unsigned char current,prev;
int current,prev;

  inptr=in;
  outptr=out;
  for (i=0;i<h;i++) {
	  for (j=0;j<w;j++) {
          	  current=*inptr++;
                  *(outptr+mapsize2)=current;
                  *outptr++=current;
                  *(outptr+mapsize2)=current;
                  *outptr++=current;
     }
     outptr+=mapsize2;
  }

}

void expand(unsigned char _HUGE *in,unsigned char _HUGE *out,int mapsiz) {
  expand_rect(in,out,mapsiz,mapsiz);
}

void expand_heights(unsigned char _HUGE *in,unsigned char _HUGE *out,int mapsiz) {

expand(in,out,mapsiz);

}
