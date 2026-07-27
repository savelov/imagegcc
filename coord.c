#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"
//#ifdef __TURBOC__
# include <malloc.h>
//#endif
#include "proj_compat.h"

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
		x=*(short *)in_ptr;
	        in_ptr+=sizeof(short);
	 }
	 if (!county) {
		county=*in_ptr++;
		y=*(short *)in_ptr;
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
	 *((short *)save_xptr)=save_x;
	 save_x=*in_ptr;
	 x=save_x+1;
	 save_xptr=out_ptr++; out_ptr+=sizeof(short);
		}
		in_ptr++;

		count_y++;
		if (*in_ptr!=y || count_y>254) {
	 *save_yptr++=count_y;
	 *((short *)save_yptr)=y;
	 y=*in_ptr;
	 count_y=0;
	 save_yptr=out_ptr++; out_ptr+=sizeof(short);
		}
		in_ptr++;
  }
  *save_xptr++=x-save_x;
  *(short *)save_xptr=save_x;
  *save_yptr++=count_y+1;
  *(short *)save_yptr=y;

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

		 xn=MapRes*(in-size_from/2)+(float)MapRes/2;   /* +MapRes/2 т.к. кв 0,0 соотв. 2km,2km */
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

       x=(x1-xc)/MapRes;              /* отбрасываем дробную часть */
       y=(yc-y1)/MapRes;              /* т.к. 3km,3km соотв. кв.0,0 */

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
      x=*((short *)in); in+=sizeof(short);
    }
    if (!county) {
      county=*in++;
      y=*((short *)in); in+=sizeof(short);
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
void walk_tables(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,
							  float MapRes, int port,int z) {
short i,x,y,countx=0,county=0;
//unsigned char *tb_ptr=tb;
unsigned char *in=tbl_ptr;
float val,jj;
int l;
int value,valt;
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
	x=*((short *)in);
        in+=sizeof(short);
		 }
    if (!county) {
       //	county=*tbl_ptr++;
       //	y=*((short *)tbl_ptr)++;
        county=*in++;
	y=*((short *)in);
        in+=sizeof(short);
		 }
    			ry=ytab-50; rx=xtab-50;
					rt=hypot((double)rx,(double)ry)*MapRes;
					l=rt;
				/*	printf("l=%i",l);*/
				value=0;valt=0;
		 if (*tb!=254 && x!=-1 && y!=-1) {
     				for(i=0; i<=port; i++) r[i]=0.;
				value=ntb[y*size_to+x];
				if(value>254) value=254;
				if (value==254) ntb[y*size_to+x]=*tb;
				else {
						 rk=10000.;
						 valt=*tb;
						 for(k=port+1; k<=MaxPorts-1; k++)
						 {
                                                 if(fk[z][k]==0) continue;
		 r[k]=hypot((double)(x-masx[k]),(double)(y-masy[k]))*MapRes;
		 rk=(rk-r[k])>=0 ? r[k] : rk;
						 }
		 rmin=(int)rk;
		 if(l<=rmin && l<100) {
			 if(value<=valt) ntb[y*size_to+x]=(char)valt;
       else
			  ntb[y*size_to+x]=(char)value;
							}
			else
		/*	printf("l-%i rmin-%i",l,rmin);*/
			 {
			 if ((value==3) && (valt>3 && valt<=18 && rmin<100)) ntb[y*size_to+x]=(char)value;
			 else   {
				if(value<=valt) ntb[y*size_to+x]=(char)valt;
				else
				 ntb[y*size_to+x]=(char)value;
				}    }
							}  /* else !=254*/
	 }
	 tb++;
	 x++;
	 countx--;
	 county--;
  }

}

void walk_table(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to) {
unsigned int i;
short x,y,countx=0,county=0;
unsigned char *in_ptr=tbl_ptr,*tb_ptr=tb;
unsigned char value;

  for (i=0;i<size_from*size_from;i++) {
	 if (!countx) {
		countx=*in_ptr++;
		x=*(short *)in_ptr;
	        in_ptr+=sizeof(short);
	 }
	 if (!county) {
		county=*in_ptr++;
		y=*(short *)in_ptr;
	        in_ptr+=sizeof(short);
	 }
	 if (*tb_ptr!=254 && x!=-1 && y!=-1) {
		value=ntb[y*size_to+x];
		if (value==254 || value<*tb_ptr) ntb[y*size_to+x]=*tb_ptr;
	 }
	 tb_ptr++;
	 x++;
	 countx--;
	 county--;
  }

}

void walk_table0(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,
							  float MapRes, int port,int z) {
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
	x=*((short *)in);
	in+=sizeof(short);

		 }
    if (!county) {
       //	county=*tbl_ptr++;
       //	y=*((short *)tbl_ptr)++;
        county=*in++;
	y=*((short *)in);
	in+=sizeof(short);

		 }
     ry=ytab-50; rx=xtab-50;
    	rt=hypot((double)rx,(double)ry)*MapRes;
	l=rt;
if ((*tb==254 && l<Rsv && x!=-1 && y!=-1)||(*tb!=254 && x!=-1 && y!=-1)) {
	for(i=0; i<MaxPorts; i++) r[i]=0.;
	 val=*(ntb+(long)y*size_to+x);
	 if(val>254.) val=254.;
	if(val==254.)
	*(ntb+(long)y*size_to+x)=*tb;
	else {
	rk=1000000.;
 	v0=pow(10.,(double)val/30.);
 	 jj=*tb;
        	for(k=port+1; k<=MaxPorts-1; k++)
 	{
//		 printf("\n %i %i",masx[port],masy[port]);
         if(fk[z][k]==0) continue;
 	 r[k]=hypot((double)(x-masx[k]),(double)(y-masy[k]))*MapRes;
 	 rk=(rk-r[k])>=0 ? r[k] : rk;
 	}
	 i=rk;
 	 if(jj==254.)  {
 	if(i>Rst)   *(ntb+(long)y*size_to+x)=254;
 	if(i<Rst)    *(ntb+(long)y*size_to+x)=val;
 						 }
  else {
//		 vt=t[(int)j];
    //     jj=jj/3.;
 	 vt=pow(10.,(double)jj/30.);
 	rmin=pow(rk,(double)cof); if(rmin==0.0) rmin=0.1;
 	rt=pow(rt,(double)cof);     if(rt==0.0)   rt=0.1;
      v=((v0/rmin+vt/rt)/(1./rmin+1./rt));
      v=log10(v)*30.;
   //   if ((flagco==0) && v<(float)prog) v=0.;
    //  {if(v<(float)prog) v=0.; }
 	 *(ntb+(long)y*size_to+x)=(char)v;
 	 }     }
     }
 	tb++;

    x++;
    countx--;
    county--;
  }
}

void interpolation (unsigned char *ntb,int map_size) {
int i,j;
unsigned char *Ptr;

 for (i=1;i<map_size-1;i++)
	for (j=1; j<map_size-1;j++) {
	  Ptr=ntb+i*map_size+j;
	  if (*Ptr==254 && *(Ptr-1)!=254 && *(Ptr+1)!=254 && *(Ptr-map_size)!=254 && *(Ptr+map_size)!=254)
	 *Ptr=(*(Ptr-1)+*(Ptr+1)+*(Ptr-map_size)+*(Ptr+map_size)+2)/4;
 }
}

void expand(unsigned char _HUGE *in,unsigned char _HUGE *out,int mapsiz) {
int i,j;
unsigned char _HUGE *inptr;
unsigned char _HUGE *outptr;
int mapsize2=mapsiz*2;
//int k;
//unsigned char current,prev;
int current,prev;

  inptr=in;
  outptr=out;
  for (i=0;i<mapsiz;i++) {
	  for (j=0;j<mapsiz;j++) {
          	  current=*inptr++;
                  *(outptr+mapsize2)=current;
                  *outptr++=current;
                  *(outptr+mapsize2)=current;
                  *outptr++=current;
     }
     outptr+=mapsize2;
  }

}

void expand_heights(unsigned char _HUGE *in,unsigned char _HUGE *out,int mapsiz) {

expand(in,out,mapsiz);

}
