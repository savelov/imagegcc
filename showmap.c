
#include <grx20.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "image.h"
#ifdef QTGUI
#include "qt_bridge.h"
#endif

int WINDOW_XSIZE=1800;
int WINDOW_YSIZE=750;

int WINDOW_LEFT=100;

#if defined(GUI) || defined(QTGUI)
int WINDOW_UP=13;
#else
int WINDOW_UP=0;
#endif

float MPIX=1;  /* 1 pixels/point , can be overriden */
int x_left=0,y_up=0;

FILE *logfile;
GrTextOption TextStyle;
GrTextOption TextStyle1;
GrFont *LabelFont=NULL;   /* cp866 font for the map labels */
GrContext *RamContext;
GrContext * SubContext;

long colors[NUM_COLORS];

#define c256 256

#define MNR    12
#define YS_V_H 3
#define UPPER 250
#define LBP  0       /* left boundary palette (on display) */
#define UBP  100       /* upper boundary palette (on display) */
#define SP   94
#define XP   9
#define YP   9

struct info *grafs;
int max_grafs;

int current_color;
int ot;
void my_circle(int x_center,int y_center,int radius) {
    GrCircle(x_center,y_center,radius,colors[current_color]);
}

void  my_pixel(int x,int y) {
    GrPlot(x,y,colors[current_color]);
}

void  my_line(int x1,int y1,int x2,int y2) {
  GrLine(x1,y1,x2,y2,colors[current_color]);
}

void my_poly(int numpoints,int  polypoints[]) {
int i;
   for(i=0; i<numpoints-1; i++)
       my_line(polypoints[2*i],polypoints[2*i+1],
	       polypoints[2*i+2],polypoints[2*i+3]);
}

void my_textxy(int x,int y,char text[]) {
   GrDrawString(text,strlen(text),x,y,&TextStyle1);
}

void outtextxy(int x,int y,char *text) {
     GrDrawString(text,strlen(text),x,y,&TextStyle);
}

void solid_bar(int x1,int y1,int x2,int y2,int color) {
    GrFilledBox(x1,y1,x2,y2,colors[color]);
}

long clev[256];        /* colors <-> levels */
char palmsg[PMSG][c10]; /* palette message */
int imap=0;

int exist (unsigned char *name) {
FILE *plik;

if ((plik=fopen (name,"rb"))==NULL)  {  fclose (plik);  return(0);  }
  else {  fclose (plik);  return(1);  }
} /* end exist */


void read_color_palette (unsigned char *naz)  {
int	   i,col1,col2,col3,col4;
FILE          *dan;

dan=fopen (naz,"rt");
if (dan==NULL)	{
   fprintf (logfile,"Error in file - %s\n",naz);  exit(1);
} /* end if */

for (i=0;i<NUM_COLORS;i++) {
  fscanf (dan,"%i %i %i %i",&col1,&col2,&col3,&col4);
  colors[i]=GrAllocColor(col1*64,col2*64,col3*64);
}
fclose (dan);

} /* end read color palette */

void color_level (char *levels) {
int  i,j,k,pom,ip,zn,zd,y,col1,col2,col3,col4,port;
char s1[80],s2[80];
static char old_level[80]="";

FILE *dan;
/* --- */
if (!strcmp(old_level,levels)) return;

strcpy(old_level,levels);
/***---- read levels & descriptions file */
if ((dan=fopen (levels,"rt"))==NULL) {
  fprintf (logfile,"\n\a5: Error in file - %s\n",levels);  exit(3);
} /* end if */

memset (palmsg,' ',PMSG*c10);
if (fgets (palmsg[0],c10-1,dan)==NULL) {
  fprintf (logfile,"\n\a5: Error in file - %s\n",levels);  exit(4); }
ip=0; while( (palmsg[0][ip]!='\n')&&(ip<c10) ) ip++;       palmsg[0][ip]=0;
memset (clev,0x00,c256);  pom=i=0;

y=UBP+16*(YP+YS_V_H);
GrFilledBox(LBP,y,LBP+SP,UBP,colors[0]);
/*GrFilledBox(WINDOW_LEFT+WINDOW_XSIZE,y,WINDOW_LEFT+WINDOW_XSIZE+SP,UBP,colors[0]);*/
y-=YP+YS_V_H;

for (k=1; k<15; k++) {
  memset(s1,0,80);
  if (fgets (s1,79,dan)==NULL)  {
    printf ("\n\a5: Error in file - %s\n",levels);  break;  }
  ip=0; while( (s1[ip]!='\n')&&(s1[ip]!=' ')&&(s1[ip]!=0x00) ) ip++;
  zn=ip;
  while ((s1[ip]==' ')&&(s1[ip]!='\n')&&(s1[ip]!=0x00)&&(ip<80)) ip++;
  zd=++ip; ip=0;
  while ((s1[ip+zd]!='\n')&&(s1[ip+zd]!=0x00)&&(ip<c10)&&(ip+zd<80)) {
  palmsg[k][ip]=s1[ip+zd];  ip++; }
  palmsg[k][ip]=0;
  s1[zn]=0; pom=atoi(s1);
 GrFilledBox(LBP,y,LBP+XP,y-YP,colors[k]);

  y-=YP+YS_V_H;
  for (; i<=pom; clev[i++]=colors[k]);
  if (i>=c256) break;
}/*  end for k */

clev[254]=clev[255]=colors[0];  fclose (dan);
for (zn=0; zn<PMSG; zn++) palmsg[zn][c10-1]=0x00;

y=UBP+16*(YP+YS_V_H);

for (j=0; j<15; j++) {
  outtextxy(LBP+XP+4,y-YP-2,palmsg[j]);
  y-=YP+YS_V_H;
} /* end for j */

y=UBP+16*(YP+YS_V_H);
 GrBox(LBP,y-YP,LBP+XP,y,colors[15]);

}        /* end color level */

#ifdef QTGUI
/* The Qt front end shows the palette strip in a pane of its own, so that
 * it stays put while the map scrolls.  Report where it lives on the
 * surface; the constants above are the authority. */
void qt_legend_rect(int *x,int *y,int *w,int *h)
{
   *x = LBP;
   *y = 0;                                   /* clock and date sit above */
   *w = WINDOW_LEFT;                         /* everything left of the map */
   *h = UBP + 16*(YP+YS_V_H) + YP;           /* down to the last swatch */
}
#endif /* QTGUI */

void draw_line(int x_start,int y_coord,unsigned char *buf,int count) {
int i;
long color;
int x_coord=0;

  color=clev[buf[0]];
  for (i=1;i<count;i++)
    if (clev[buf[i]]!=color) {
       GrFilledBox(x_coord*MPIX+x_start,y_coord,i*MPIX-1+x_start,y_coord+MPIX,color);
       x_coord=i;
       color=clev[buf[i]];
   }
   GrFilledBox(x_coord*MPIX+x_start,y_coord,count*MPIX-1+x_start,y_coord+MPIX,color);

}

void close_graph(void) {
int i;

  for (i=0;i<NUM_COLORS; i++) GrFreeColor(colors[i]);
  GrDestroyContext(RamContext);
//  GrSetMode(GR_default_text);
  fclose(logfile);

}

int init_graph(char *palette_file) {
int i;
int svga;
GrContext * context;


   logfile=stdout;
#if defined(QTGUI)
   /* Qt owns the window: render through the GRX memory driver, in the same
    * mode the X11 build uses, and let the Qt widget show that surface. */
   svga=2;
   if (!GrSetDriver("memory")) {
      fprintf(logfile,"Can't select the GRX memory driver\n");
      exit(1);
   }
   if (!GrSetMode(GR_width_height_color_graphics,
                  QT_SCREEN_XSIZE,QT_SCREEN_YSIZE,256*256*256L)) {
      fprintf(logfile,"Can't set a %dx%d truecolor memory mode\n",
              QT_SCREEN_XSIZE,QT_SCREEN_YSIZE);
      exit(1);
   }
   qt_set_screen_context(GrScreenContext());
#elif defined(GUI)
   GrMouseDetect();
   svga=2;
   GrSetMode(GR_width_height_color_graphics,1920,1080,256*256*256L); 
#else 
   context=GrCreateFrameContext(GR_frameRAM8,WINDOW_LEFT,WINDOW_UP+WINDOW_YSIZE,NULL,NULL);
   if (context == NULL)  {
      fprintf(logfile,"Can't allocate memory context for SVGA1 buffer\n");
      exit(1); 
   }
   GrSetContext(context);
#endif
 
   read_color_palette(palette_file);
  // GrClearScreen(colors[0]);
    GrClearContext(GrBlack());

  /* The labels are cp866 text, which the built-in cp437 font cannot show.
   * cp866-8x14.psf holds the same VGA glyphs in cp866 order; if it is
   * missing we still run, just with unreadable cyrillic. */
  {
    char font_file[100];
    sprintf(font_file,"%s/cp866-8x14.psf",cfgdir);
    LabelFont = GrLoadFont(font_file);
    if (LabelFont == NULL)
      printf("cannot load %s, cyrillic labels will be garbled\n",font_file);
  }

  memset(&TextStyle,0,sizeof(TextStyle));
   TextStyle.txo_font      = LabelFont ? LabelFont : &GrDefaultFont;
   TextStyle.txo_xalign    = GR_ALIGN_LEFT;
   TextStyle.txo_yalign    = GR_ALIGN_TOP;
   TextStyle.txo_direct    = GR_TEXT_RIGHT;
   TextStyle.txo_fgcolor.v = colors[15];
   TextStyle.txo_bgcolor.v = colors[0];
   memset(&TextStyle1,0,sizeof(TextStyle1));
   TextStyle1.txo_font      = LabelFont ? LabelFont : &GrDefaultFont;
   TextStyle1.txo_xalign    = GR_ALIGN_LEFT;
   TextStyle1.txo_yalign    = GR_ALIGN_TOP;
   TextStyle1.txo_direct    = GR_TEXT_RIGHT;
   TextStyle1.txo_fgcolor.v = colors[15];
   TextStyle1.txo_bgcolor.v = GrOR;
#if defined(GUI) || defined(QTGUI)
   RamContext=GrCreateFrameContext(GR_frameRAM24,WINDOW_XSIZE,WINDOW_YSIZE,NULL,NULL);
   SubContext=GrCreateSubContext(WINDOW_LEFT,WINDOW_UP,
     WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE,NULL,NULL);

#else
   RamContext=GrCreateFrameContext(GR_frameRAM8,WINDOW_XSIZE,WINDOW_YSIZE,NULL,NULL);
#endif
   if (RamContext!=NULL) return 0;
   fprintf(logfile,"Can't allocate memory context for SVGA buffer\n");
   close_graph();
   return 1;
}

void show_vectors(void) {
int port,kx,ky,kx1,ky1;
float X,Y, xv,yv , lat, lon ;
char s[80];

  for (port=0;port<MaxPorts;port++) {

      conv_back(LU,BU,L0[port],B0[port],&X,&Y);
      kx=X*MPIX/MRES-x_left*MPIX+WINDOW_XSIZE/2;
      ky=-Y*MPIX/MRES-y_up*MPIX+WINDOW_YSIZE/2;


    if (SPEED[port]!=-1)  {
      xv=(float)SPEED[port]*sin(AZIMUT[port]*RAD);
      yv=(float)SPEED[port]*cos(AZIMUT[port]*RAD);
      convert_port (L0[port],B0[port],xv,yv,&lon,&lat);
      conv_back(LU,BU,lon,lat,&X,&Y);

      kx1=X*MPIX/MRES-x_left*MPIX+WINDOW_XSIZE/2;
      ky1=-Y*MPIX/MRES-y_up*MPIX+WINDOW_YSIZE/2;

      GrLine(kx,ky,kx1,ky1,colors[15]);
      GrLine(kx+1,ky,kx1+1,ky1,colors[15]);
      GrLine(kx-1,ky,kx1-1,ky1,colors[15]);
      GrLine(kx,ky+1,kx1,ky1+1,colors[15]);
      GrLine(kx,ky-1,kx1,ky1-1,colors[15]);
    }
  }
}

int draw_map(int vectors) {

GrContext ScreenContext;
int i;
char thresh_file[100];
int x_begin,x_size,y_begin,y_size,y_start,x_start;
long mapsize=MSIZE;

sprintf(thresh_file,"%s/thresh.%s",grfdir,maps[current_map].thresh);
color_level(thresh_file);
outtextxy(LBP,UBP,maps[current_map].descr);

GrSaveContext(&ScreenContext);
GrSetContext(RamContext);

//GrClearContext(colors[15]);

/* MRES is 0 when the current product is missing from this file - animate()
 * loads a single product, and not every file carries all of them.  linear()
 * and show_vectors() divide by it, which turns every coordinate into
 * INT_MIN and takes GRX down, so keep the previous frame instead. */
if (MRES>0) {

x_begin=mapsize/2-WINDOW_XSIZE/2/MPIX+x_left;
x_size=WINDOW_XSIZE/MPIX+2;
y_begin=mapsize/2-WINDOW_YSIZE/2/MPIX+y_up;
y_size=WINDOW_YSIZE/MPIX+2;


i=WINDOW_XSIZE/2/MPIX; x_start=WINDOW_XSIZE/2-i*MPIX; if (x_start) { x_begin--; x_start-=MPIX; }
i=WINDOW_YSIZE/2/MPIX; y_start=WINDOW_YSIZE/2-i*MPIX; if (y_start) { y_begin--; y_start-=MPIX; }
//x_start=(WINDOW_XSIZE%(int)(MPIX*2))/2; if (x_start) { x_begin--; x_start-=MPIX; }
//y_start=(WINDOW_YSIZE%(int)(MPIX*2))/2; if (y_start) { y_begin--; y_start-=MPIX; }

for (i=0;i<y_size;i++)
  draw_line(x_start,i*MPIX+y_start,(unsigned char *)(mapbuffer+mapsize*(i+y_begin)+x_begin),x_size);

draw_tlo(grafs,max_grafs);
if (vectors) show_vectors();

}   /* MRES>0 */


#if defined(QTGUI)
qt_compose_map();   // put the map on the screen surface Qt shows
#elif defined(GUI)
// copy RAM to screen

GrSaveContextToPng(RamContext,"/tmp/output.png"); 
GrLoadContextFromPng(SubContext,"/tmp/output.png",0);

//GrBitBlt (&ScreenContext,WINDOW_LEFT,WINDOW_UP,RamContext,0,0,WINDOW_XSIZE-1,WINDOW_YSIZE-1,GrWRITE);
//GrImage* img_local = GrImageFromContext(RamContext);
//GrImageDisplay(WINDOW_LEFT,WINDOW_UP, img_local);
#endif

GrSetContext(&ScreenContext);

GrBox(WINDOW_LEFT-1,WINDOW_UP-1,WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE,GrAllocColor(255,255,255));

show_header();
#if defined(QTGUI)
/* lets animate() repaint and stay interruptible: space stops it */
return qt_poll_key();
#elif defined(GUI)
return message_poll();
#else
return 0;
#endif
}

void set_cursor(void) {

	static char ptr16x16bits[]= {
	    2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	    2,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
	    2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,
	    2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,
	    2,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,
	    2,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,
	    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,
	    2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,
	    2,1,1,1,1,1,2,2,0,0,0,0,0,0,0,0,
	    2,1,2,2,2,1,1,2,0,0,0,0,0,0,0,0,
	    2,2,0,0,2,1,1,2,0,0,0,0,0,0,0,0,
	    0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,
	    0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,
	    0,0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,
	    0,0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,
	    0,0,0,0,0,0,0,2,1,1,2,0,0,0,0,0,
	};

	GrCursor *newc;
	GrColor cols[3];
	cols[0] = 2;
	cols[1] = colors[15];
	cols[2] = colors[0];
	newc = GrBuildCursor(ptr16x16bits,16,16,16,1,1,cols);
	if(!newc) return;
	GrMouseSetCursor(newc);
}
int message_poll(void) {
GrMouseEvent event;
        GrMouseGetEvent(GR_M_KEYPRESS | GR_M_POLL,&event);
	mouse_move(event.x,event.y);
	if (event.flags&GR_M_KEYPRESS) return event.key; else return 0;
}
void message_loop(void)
{
GrMouseEvent event;
int quitflag=0;
int prevx=0,prevy=0;
char thresh_file[100];

	//set_cursor();
        GrMouseDisplayCursor();

	while (!quitflag)
 {
	  timer();
	  GrMouseGetEventT(GR_M_KEYPRESS | GR_M_MOTION | GR_M_LEFT_DOWN | GR_M_RIGHT_DOWN,&event,1000);
	  if (event.flags&GR_M_KEYPRESS)   quitflag=key_pressed(event.key);
	  if (event.flags&GR_M_MOTION)     mouse_move(event.x,event.y);
	  if (event.flags&GR_M_RIGHT_DOWN)  {
		/*if (abs(event.x-prevx)>MPIX || abs(event.y-prevy)>MPIX) {*/
		     if (mouse_click_left(event.x,event.y)) {
                             GrMouseWarp(WINDOW_LEFT+WINDOW_XSIZE/2,
					 WINDOW_UP+WINDOW_YSIZE/2);
                             prevx=WINDOW_LEFT+WINDOW_XSIZE/2;
			     prevy=WINDOW_UP+WINDOW_YSIZE/2;
                     } else {
                          prevx=event.x;
                          prevy=event.y;
                     }
		/*} else {
                     mouse_dclick_left(event.x,event.y);
                     prevx=event.x;
                     prevy=event.y;
		} */
	  }
	  if (event.flags&GR_M_LEFT_DOWN)

  {if(flagl==0)
		{
		 if(flagv==1)
		 {
		// solid_bar(WINDOW_LEFT-2,WINDOW_UP-1,WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE+2,0);
		read_files(cur_file,0);	 draw_map(1); flagv=0;flagl=0;}
		 else if(flagv==0)
		 {
	//	 solid_bar(WINDOW_LEFT-2,WINDOW_UP-1,WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE+2,0);
		/*	read_files(cur_file,0);*/ draw_map(1);

		 xc1=xck; yc1=yck;
		 xco1=xco/2; yco1=yco/2;
		 flagl=1;
    GrMouseEraseCursor();
    GrMouseSetCursorMode(GR_M_CUR_LINE,event.x,event.y,colors[15]);
    GrMouseDisplayCursor();

		 }
		 }
   else
		{

    GrMouseEraseCursor();
    GrMouseSetCursorMode(GR_M_CUR_NORMAL);
    GrMouseDisplayCursor();

		xc2=xck; yc2=yck; xco2=xco/2; yco2=yco/2;
		sprintf(thresh_file,"%s/thresh.z",grfdir);
		color_level(thresh_file);
		outtextxy(LBP,UBP,maps[4].descr);

		    vert(xco1,yco1,xco2,yco2);
			    flagl=0;
		}

  }
 }

}
