#include <math.h>
#include <stdio.h>
#include <time.h>
#include "image.h"
#ifdef QTGUI
#include "qt_bridge.h"
#endif

#ifdef __GNUC__
#ifdef __MSDOS__
#include <dpmi.h>
#endif
#endif

#ifdef __TURBOC__
#include <alloc.h>
#endif

#define LBT    3       /* left boundary ?? tabelki (on display), >13 */
#define UBT    150          /* upper boundary ?? (on display) */
#define WDT    76          /* width space of ??, > 66 */
#define HDT    200         /* height of ?? */

#define UPPER  100

void show_header(void) {
char temp[80];

   sprintf(temp,"%2d/%02d/%4d",header[2],header[1],header[0]+2000);
   outtextxy(0,28,temp);
   sprintf(temp,"%2d:%02d",header[3],header[4]);
   outtextxy(16,42,temp);

}


void showdata(int xpix,int ypix) {
int dolg_gr,dolg_min,shir_gr,shir_min;
double temp;
float X,Y,X2,Y2,Fir,Lar;
int radius,azym;
char string[80];
int mapsize=MSIZE;
int xcoord,ycoord;
int it ,k, k1,port,l ;
char it1[5],s[80];
float i;

  if (xpix>0) xpix--;
  if (ypix>0) ypix--;
  xcoord=mapsize/2+xpix+x_left;
  ycoord=mapsize/2+ypix+y_up;
  if (xcoord<0) xcoord=0;
  if (ycoord<0) ycoord=0;

  convert(LU,BU,MRES*(xpix+x_left)+MRES/2,
		-MRES*(ypix+y_up)-MRES/2,&Lar,&Fir);
  dolg_min=modf(Lar*GR,&temp)*60+.5;
  dolg_gr=temp;
  if (dolg_min==60) { dolg_min=0; dolg_gr++; }
  shir_min=modf(Fir*GR,&temp)*60+.5;
  shir_gr=temp;
  if (shir_min==60) { shir_min=0; shir_gr++; }

  conv_back(LC,BC,Lar,Fir,&X,&Y);
  X2=X*X;
  Y2=Y*Y;
  radius=sqrt(X2+Y2)+.5;
  if (Y==0.) azym= X>=0. ? (X==0. ? 0 : 90) : 270;
    else {
    azym=atan2(X,Y)*GR+.5;
    azym= azym>=0 ? azym: 360+azym;
  } /* end else if Y=0 */

  sprintf(string,"x= %2d¯ %02d",dolg_gr,dolg_min);
    outtextxy(WINDOW_LEFT+WINDOW_XSIZE+3,UPPER+16*14,string);
  sprintf(string,"y= %2d¯ %02d",shir_gr,shir_min);
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+3,UPPER+17*14,string);
  sprintf(string,"r=%4i km", radius);
     outtextxy(WINDOW_LEFT+WINDOW_XSIZE+3,UPPER+18*14,string);
  sprintf(string,"az=%3i¯",azym);
      outtextxy(WINDOW_LEFT+WINDOW_XSIZE+3,UPPER+19*14,string);
  sprintf(string,"MPIX=%.2f",MPIX);
      outtextxy(WINDOW_LEFT+WINDOW_XSIZE+3,UPPER+20*14,string);
//  sprintf(string,"[%3d,%3d]=%3d",xcoord,ycoord,*(mapbuffer+(long)ycoord*mapsize+xcoord));
//  sprintf(string,"[x,y]=%3d",*(mapbuffer+(long)ycoord*mapsize+xcoord));
//  outtextxy(LBT+3,UBT+HDT+3+5*14,string);
//  sprintf(string,"%5.1f/%3d¯",SPEED[0],AZIMUT[0]);
//  if (SPEED[0]==-1)  sprintf(string,"?????/????");
//  outtextxy(LBT+3,UBT+HDT+3+6*14,string);

// #ifdef __GNUC__
// #ifdef __MSDOS__
// {
// long kbytes=_go32_dpmi_remaining_virtual_memory()/1024;
// sprintf(string,"%5ldK",kbytes);
// outtextxy(LBT+3,UBT+HDT+3+7*14,string);
// }
// #endif
// #endif
// #ifdef __TURBOC__
//	{
//	long bytes=coreleft();
//	sprintf(string,"%5ld",bytes);
//	outtextxy(LBT+3,UBT+HDT+3+7*14,string);
//	}
// #endif

//  sprintf(string,"Ääëéèêà");
// outtextxy(LBT+3,UBT+HDT+3+9*14,string);
//  sprintf(string,"ñÄé");
// outtextxy(LBT+5,UBT+HDT+3+10*14,string);
//    sprintf(string,"åéëäÇÄ");
//  outtextxy(LBT+3,UBT+HDT+3+11*14,string);
   it = get_map_data(MAP_S,xcoord,ycoord);
	 k1=0;
	 sprintf(s,"              ");   /* clears the previous S= line */
   if (it!=254) {
/*   if(flagh==0)
       {*/
			 for (k1=0; k1<15; k1++)
	 if (it<=sto[k1]) break;
  sprintf(string,"S=%s",stormmsg[k1]);
	 }
	 else  sprintf(string,"S=%s",palmsg[0]);

	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+2*14,s);
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+2*14,string);

	 it = get_map_data(MAP_P,xcoord,ycoord);
	 if(it!=254)
	 {i  = it;
   i = (i-69.) / 48.; i = pow(10.,i);
	 sprintf(string,"P=%4.1f¨¨/Á",i);
	 }
	 else  sprintf(string,"P=%s ",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+3*14,string);

	 it = get_map_data(MAP_H,xcoord,ycoord);
	 if(it!=254)
	 {
   i  = it; i=i/10.;
	 sprintf(string,"H=%4.1f™¨ ",i);
	 }
		else  sprintf(string,"H=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+4*14,string);

	 it = get_map_data(MAP_Q,xcoord,ycoord);
	 if(it!=254)
	 {
   i  = it;
	 i = i / 64.;/* i = pow(10.,i)/10.;*/ it=pow(10.,i)-0.5; i=(float)it/10.;
	 sprintf(string,"Q=%4.1f¨¨ ",i);
	 } else  sprintf(string,"Q=%s ",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+5*14,string);

it = get_map_data(MAP_8,xcoord,ycoord);
if(it!=254) sprintf(string,"8=%2d dbz ",(it+1)/3);
			else  sprintf(string,"8=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+6*14,string);
it = get_map_data(MAP_7,xcoord,ycoord);
if(it!=254)	sprintf(string,"7=%2d dbz ",(it+1)/3);
			else  sprintf(string,"7=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+7*14,string);
it = get_map_data(MAP_6,xcoord,ycoord);
if(it!=254)		sprintf(string,"6=%2d dbz ",(it+1)/3);
				else  sprintf(string,"6=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+8*14,string);
it = get_map_data(MAP_5,xcoord,ycoord);
if(it!=254)	 	sprintf(string,"5=%2d dbz ",(it+1)/3);
				else  sprintf(string,"5=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+9*14,string);
it = get_map_data(MAP_4,xcoord,ycoord);
if(it!=254)	sprintf(string,"4=%2d dbz ",(it+1)/3);
			else  sprintf(string,"4=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+10*14,string);
it = get_map_data(MAP_3,xcoord,ycoord);
if(it!=254)	sprintf(string,"3=%2d dbz ",(it+1)/3);
			else  sprintf(string,"3=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+11*14,string);
it = get_map_data(MAP_2,xcoord,ycoord);
if(it!=254)	sprintf(string,"2=%2d dbz ",(it+1)/3);
			else  sprintf(string,"2=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+12*14,string);
it = get_map_data(MAP_1,xcoord,ycoord);
if(it!=254)	sprintf(string,"1=%2d dbz ",(it+1)/3);
			else  sprintf(string,"1=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+13*14,string);
it = get_map_data(MAP_0,xcoord,ycoord);
if(it!=254)	sprintf(string,"0=%2d dbz ",(it+1)/3);
			else  sprintf(string,"0=%s",palmsg[0]);
	 outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+14*14,string);

    }

   /* if(flagh==1) solid_bar(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+1*14,
			   WINDOW_LEFT+WINDOW_XSIZE+110,UPPER+15*14,0);
      } */
/*	 else  {
  memset(string,0,80);
   sprintf(string,"S=≠•‚ ®≠‰.");
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+2*14,string);
   sprintf(string,"P=%6d         ",get_map_data(MAP_P,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+3*14,string);
   sprintf(string,"H=%6d    ",get_map_data(MAP_H,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+4*14,string);
   sprintf(string,"Q=%6d    ",get_map_data(MAP_Q,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+5*14,string);
   sprintf(string,"8=%6d",get_map_data(MAP_8,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+6*14,string);
   sprintf(string,"7=%6d",get_map_data(MAP_7,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+7*14,string);
   sprintf(string,"6=%6d",get_map_data(MAP_6,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+8*14,string);
   sprintf(string,"5=%6d",get_map_data(MAP_5,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+9*14,string);
   sprintf(string,"4=%6d",get_map_data(MAP_4,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+10*14,string);
   sprintf(string,"3=%6d",get_map_data(MAP_3,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+11*14,string);
   sprintf(string,"2=%6d",get_map_data(MAP_2,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+12*14,string);
   sprintf(string,"1=%6d",get_map_data(MAP_1,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+13*14,string);
   sprintf(string,"0=%6d",get_map_data(MAP_0,xcoord,ycoord));
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+14*14,string);
      }

} */

#ifdef QTGUI
/* Likewise for the cursor readout column down the right hand side. */
void qt_readout_rect(int *x,int *y,int *w,int *h)
{
   *x = WINDOW_LEFT + WINDOW_XSIZE;
   *y = UPPER - 14;                          /* first line is UPPER+2*14 */
   *w = 15*8;                                /* widest line is ~12 chars */
   *h = 23*14;                               /* last one is UPPER+20*14 */
}
#endif /* QTGUI */

void showtime(void) {
time_t timer;
struct tm *cur_time;
char temp[80];

 time(&timer);
 cur_time=localtime(&timer);
 sprintf(temp,"%2d:%02d:%02d",cur_time->tm_hour,cur_time->tm_min,cur_time->tm_sec);
 outtextxy(LBT+3,0,temp);
}
