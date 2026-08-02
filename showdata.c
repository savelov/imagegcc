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


/* The readout column down the right hand side. */
#define READOUT_W    (24*8)
#define READOUT_ROWS 42

static void line(int row,char *text)
{
   outtextxy(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+row*14,text);
}

/* One reading in its product's own units.  The conversions are the AKSOPRI
 * ones from cao/conversion.py: the byte scales bufr2wrk.py writes, and the
 * same ones mkpalettes.py coloured the palettes by. */
static void format_reading(int index,int value,char *text)
{
double v;

   if (value<0) { sprintf(text,"= %s",palette_nodata_label()); return; }
   if (value==maps[index].noecho) {
      sprintf(text,"= %s",palette_label_of(maps[index].palette,value));
      return;
   }

   switch (maps[index].family) {
   case FAM_DBZ:
      sprintf(text,"= %d dBZ",value/3);
      break;
   case FAM_RAIN:
      sprintf(text,"= %.1f ¬¬/ç",pow(10.,(double)value/48.-1.4375));
      break;
   case FAM_SUM:
      sprintf(text,"= %.1f ¬¬",(pow(10.,(double)value/64.)-0.5)*0.1);
      break;
   case FAM_ZDR:
      /* debufr wraps this scale: bytes from 123 up are 10*ZDR+127, the rest
       * are the 0.2 dB continuation (see convert_aksopri_differential_
       * reflectivity_byte) */
      v=(value>=123) ? (value-127)*0.1 : (value+1)*0.1;
      sprintf(text,"= %.1f ¤",v);
      break;
   case FAM_VEL:
      sprintf(text,"= %.1f ¬/á",(value-127)/2.0);
      break;
   case FAM_HEIGHT:
      sprintf(text,"= %.1f ª¬",value/10.0);
      break;
   case FAM_PHENOM:
   default:
      sprintf(text,"= %s",palette_label_of(maps[index].palette,value));
      break;
   }
}

/* One of the summary products, on its own line: "H= 3.4 ª¬" */
static void summary(char tag,map_type id,int xcoord,int ycoord,int *row)
{
int index=map_index(id);
char text[80],value[80];

   format_reading(index,map_value(id,xcoord,ycoord),value);
   sprintf(text,"%c%s",tag,value);
   line((*row)++,text);
}

/* xcoord,ycoord are mapbuffer cells - the cell painted under the cursor, as
 * surface_to_map() works it out.  This used to be handed the offset from the
 * window centre and repeat the arithmetic itself, which is how the readout
 * came to describe a neighbouring cell once zoomed in. */
void showdata(int xcoord,int ycoord) {
int dolg_gr,dolg_min,shir_gr,shir_min;
double temp;
float X,Y,X2,Y2,Fir,Lar;
int radius,azym;
char string[80];
int mapsize=MSIZE;
int it,k,row;
char s[80];

  convert(LU,BU,MRES*(xcoord-mapsize/2)+MRES/2,
		-MRES*(ycoord-mapsize/2)-MRES/2,&Lar,&Fir);
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

  /* the readout column, all of it, so that a product with fewer rows than
   * the last one leaves nothing of it behind */
  solid_bar(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER,
            WINDOW_LEFT+WINDOW_XSIZE+READOUT_W,UPPER+READOUT_ROWS*14,0);

  row=0;
  sprintf(string,"x= %2dø %02d",dolg_gr,dolg_min);   line(row++,string);
  sprintf(string,"y= %2dø %02d",shir_gr,shir_min);   line(row++,string);
  sprintf(string,"r=%4i km", radius);                  line(row++,string);
  sprintf(string,"az=%3iø",azym);                    line(row++,string);
  sprintf(string,"MPIX=%.2f",MPIX);                    line(row++,string);
  row++;

  /* the product on screen, in its own units */
  line(row++,maps[current_map].descr);
  format_reading(current_map,map_value(maps[current_map].mapid,xcoord,ycoord),
                 string);
  line(row++,string);
  row++;

  /* the summary products, whatever is on screen */
  summary('S',MAP_S,xcoord,ycoord,&row);
  summary('P',MAP_P,xcoord,ycoord,&row);
  summary('H',MAP_H,xcoord,ycoord,&row);
  summary('Q',MAP_Q,xcoord,ycoord,&row);
  row++;

  /* the vertical profile: every reflectivity level the file carries, plus
   * the levels of the family on screen when that is ZDR or velocity.  All
   * three at once would be more rows than the column has. */
  for (k=0;k<no_maps;k++) {
     if (!map_present(k)) continue;
     if (maps[k].family!=FAM_DBZ &&
         maps[k].family!=maps[current_map].family) continue;
     if (maps[k].family!=FAM_DBZ && maps[k].family!=FAM_ZDR &&
         maps[k].family!=FAM_VEL) continue;
     if (row>=READOUT_ROWS-1) break;
     format_reading(k,map_value(maps[k].mapid,xcoord,ycoord),s);
     sprintf(string,"%s %s",maps[k].descr,s);
     line(row++,string);
  }

    }

   /* if(flagh==1) solid_bar(WINDOW_LEFT+WINDOW_XSIZE+1,UPPER+1*14,
			   WINDOW_LEFT+WINDOW_XSIZE+110,UPPER+15*14,0);
      } */
/*	 else  {
  memset(string,0,80);
   sprintf(string,"S=­¥â ¨­ä.");
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
/* Repaint the readout where it now lives.  It is normally drawn only as the
 * cursor moves, so after a resize the column would stay blank until the mouse
 * happened to cross the map. */
void qt_redraw_readout(void)
{
   showdata(xco,yco);
}

void qt_readout_rect(int *x,int *y,int *w,int *h)
{
   *x = WINDOW_LEFT + WINDOW_XSIZE;
   *y = UPPER;
   *w = READOUT_W;
   *h = READOUT_ROWS*14;
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
