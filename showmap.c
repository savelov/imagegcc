
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

int imap=0;

int exist (unsigned char *name) {
FILE *plik;

if ((plik=fopen (name,"rb"))==NULL)  {  fclose (plik);  return(0);  }
  else {  fclose (plik);  return(1);  }
} /* end exist */


/* config/palette holds the user interface colours - background, text, frames,
 * cursor.  The data colours live in the per product tables palette.c loads,
 * so these 16 are not a limit on anything the map shows. */
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

#ifdef QTGUI
/* The Qt front end shows the legend in a pane of its own, so that it stays
 * put while the map scrolls.  palette.c owns the geometry. */
void qt_legend_rect(int *x,int *y,int *w,int *h)
{
   palette_rect(x,y,w,h);
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

#if defined(GUI) || defined(QTGUI)
/* Left button handling for the vertical cross section, factored out of the
 * X11 event loop so the Qt front end can drive it too.  First click marks
 * the start, the second draws the section, and a click while one is on
 * screen puts the map back.  The caller must have fed the current cursor
 * position to mouse_move() first: xck/yck/xco/yco come from there. */
void cross_section_click(void)
{
   if (flagl == 0) {
      if (flagv == 1) {                 /* a section is showing: restore */
         read_files(cur_file,0);
         draw_map(1);
         flagv = 0;
         flagl = 0;
         return;
      }
      draw_map(1);
      xc1 = xck; yc1 = yck;
      xco1 = xco/2; yco1 = yco/2;
      flagl = 1;                        /* start point taken */
   } else {
      xc2 = xck; yc2 = yck;
      xco2 = xco/2; yco2 = yco/2;
      /* the section is drawn from the reflectivity levels, whatever the map
       * on screen was showing */
      set_palette("reflectivity",maps[map_index(MAP_1)].nodata);
      draw_legend(maps[map_index(MAP_1)].descr);
      vert(xco1,yco1,xco2,yco2);
      flagl = 0;
   }
}

/* 0 = idle, 1 = waiting for the second point, so the front end can say so */
int cross_section_state(void) { return flagl; }
#endif

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
   /* The batch renderer used to draw into a bare RAM8 frame context, with no
    * GrSetMode at all.  GRX then leaves its colour table at the 16 entries it
    * starts life with and GrAllocColor gives up after those - which is the
    * real 16 colour limit this program used to live under.  Take the same
    * route the Qt build does instead: the memory driver in a truecolor mode,
    * where a colour is a packed RGB triple and nothing is rationed. */
   if (!GrSetDriver("memory")) {
      fprintf(logfile,"Can't select the GRX memory driver\n");
      exit(1);
   }
   if (!GrSetMode(GR_width_height_color_graphics,
                  WINDOW_LEFT,WINDOW_UP+WINDOW_YSIZE,256*256*256L)) {
      fprintf(logfile,"Can't set a %dx%d truecolor memory mode\n",
              WINDOW_LEFT,WINDOW_UP+WINDOW_YSIZE);
      exit(1);
   }
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
   RamContext=GrCreateFrameContext(GR_frameRAM24,WINDOW_XSIZE,WINDOW_YSIZE,NULL,NULL);
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

/* Where the map sits on the surface.
 *
 * draw_line() paints mapbuffer cell x_begin+j across the pixels
 * x_start+j*MPIX .. x_start+(j+1)*MPIX-1 of the map area, and likewise down
 * the rows.  draw_map() and surface_to_map() both work from here, so that the
 * cell the cursor reports is always the cell that was painted under it.
 */
static void map_origin(int *x_begin,int *y_begin,int *x_start,int *y_start)
{
long mapsize=MSIZE;
int i;

  *x_begin=mapsize/2-WINDOW_XSIZE/2/MPIX+x_left;
  *y_begin=mapsize/2-WINDOW_YSIZE/2/MPIX+y_up;

  i=WINDOW_XSIZE/2/MPIX; *x_start=WINDOW_XSIZE/2-i*MPIX;
  if (*x_start) { (*x_begin)--; *x_start-=MPIX; }
  i=WINDOW_YSIZE/2/MPIX; *y_start=WINDOW_YSIZE/2-i*MPIX;
  if (*y_start) { (*y_begin)--; *y_start-=MPIX; }
}

/* The mapbuffer cell drawn at a point on the surface.  The cursor readout used
 * to work this out on its own, from the window centre and MPIX, which agreed
 * with the drawing to within a cell - invisible at MPIX 1, an obviously wrong
 * reading once zoomed in far enough for a cell to be several pixels wide. */
void surface_to_map(int x,int y,int *mx,int *my)
{
int x_begin,y_begin,x_start,y_start;

  map_origin(&x_begin,&y_begin,&x_start,&y_start);
  *mx=x_begin+(int)floor((double)(x-WINDOW_LEFT-x_start)/MPIX);
  *my=y_begin+(int)floor((double)(y-WINDOW_UP  -y_start)/MPIX);
  if (*mx<0) *mx=0;
  if (*my<0) *my=0;
  if (*mx>=(int)MSIZE) *mx=MSIZE-1;
  if (*my>=(int)MSIZE) *my=MSIZE-1;
}

int draw_map(int vectors) {

GrContext ScreenContext;
int i;
int x_begin,x_size,y_begin,y_size,y_start,x_start;
long mapsize=MSIZE;

set_palette(maps[current_map].palette,maps[current_map].nodata);
draw_legend(maps[current_map].descr);

GrSaveContext(&ScreenContext);
GrSetContext(RamContext);

//GrClearContext(colors[15]);

/* MRES is 0 when the current product is missing from this file - animate()
 * loads a single product, and not every file carries all of them.  linear()
 * and show_vectors() divide by it, which turns every coordinate into
 * INT_MIN and takes GRX down, so keep the previous frame instead. */
if (MRES>0) {

map_origin(&x_begin,&y_begin,&x_start,&y_start);
x_size=WINDOW_XSIZE/MPIX+2;
y_size=WINDOW_YSIZE/MPIX+2;

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
		set_palette("reflectivity",maps[map_index(MAP_1)].nodata);
		draw_legend(maps[map_index(MAP_1)].descr);

		    vert(xco1,yco1,xco2,yco2);
			    flagl=0;
		}

  }
 }

}
