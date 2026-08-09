
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

/* Fit the map to the canvas.  The canvas used to be nailed to the size
 * image.cfg asked for, so maximising the window only added empty scroll area
 * around a map that never grew.  The surface is the hard limit, and the
 * readout column has to keep its place to the right of the map. */
int qt_resize_map(int w,int h)
{
   int rx,ry,rw,rh;
   int maxw,maxh;

   qt_readout_rect(&rx,&ry,&rw,&rh);
   maxw=qt_surface_width()-WINDOW_LEFT-rw;
   maxh=qt_surface_height()-WINDOW_UP;

   if (w>maxw) w=maxw;
   if (h>maxh) h=maxh;
   if (w<MIN_MAP_SIZE) w=MIN_MAP_SIZE;
   if (h<MIN_MAP_SIZE) h=MIN_MAP_SIZE;
   if (w==WINDOW_XSIZE && h==WINDOW_YSIZE) return 0;

   WINDOW_XSIZE=w;
   WINDOW_YSIZE=h;
   /* The surface keeps whatever the previous, differently sized panes left on
    * it - a shrinking map leaves its old right hand edge behind, exactly where
    * the readout column now has to go.  Start from a clean sheet; draw_map()
    * and the readout repaint everything that belongs on it. */
   GrClearContext(colors[0]);
   if (RamContext!=NULL) GrDestroyContext(RamContext);
   RamContext=GrCreateFrameContext(GR_frameRAM24,WINDOW_XSIZE,WINDOW_YSIZE,NULL,NULL);
   if (RamContext==NULL) {
      fprintf(logfile,"Can't allocate a %dx%d map buffer\n",WINDOW_XSIZE,WINDOW_YSIZE);
      exit(1);
   }
   update_coords();          /* the pan limits are in units of window size */
   return 1;
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

static void map_origin(int *x_begin,int *y_begin,int *x_start,int *y_start);

#if defined(GUI) || defined(QTGUI)
/* Left button handling for the vertical cross section, factored out of the
 * X11 event loop so the Qt front end can drive it too.  First click marks
 * the start, the second takes the section.  The caller must have fed the
 * current cursor position to mouse_move() first: xck/yck/xco/yco come from
 * there.
 *
 * Where the section then goes differs between the two front ends.  imagegcc
 * has nowhere to put it but the map window, so vert() paints over the map and
 * a further click reloads the file to get the map back.  The Qt build shows it
 * in a window of its own - see crosssect.c and the CrossSectionWindow - and
 * leaves the map alone, drawing only the line that was cut. */
void cross_section_click(void)
{
   if (flagl == 0) {
#ifndef QTGUI
      if (flagv == 1) {                 /* a section is showing: restore */
         read_files(cur_file,0);
         draw_map(1);
         flagv = 0;
         flagl = 0;
         return;
      }
#endif
      xc1 = xck; yc1 = yck;
      xco1 = xco/2; yco1 = yco/2;
      flagl = 1;                        /* start point taken */
      flagv = 0;                        /* the previous cut is not this one */
      draw_map(1);
   } else {
      xc2 = xck; yc2 = yck;
      xco2 = xco/2; yco2 = yco/2;
      flagl = 0;
#ifdef QTGUI
      flagv = 1;                        /* both ends taken: draw the cut */
      draw_map(1);
#else
      /* the section is drawn from the reflectivity levels, whatever the map
       * on screen was showing */
      set_palette("reflectivity",maps[map_index(MAP_1)].nodata);
      draw_legend(maps[map_index(MAP_1)].descr);
      vert(xco1,yco1,xco2,yco2);
#endif
   }
}

/* 0 = idle, 1 = waiting for the second point, so the front end can say so */
int cross_section_state(void) { return flagl; }

#ifdef QTGUI
/* The line that was cut, in product grid cells - what cross_section_compute()
 * takes and what vert() is handed. */
void cross_section_endpoints(int *x1,int *y1,int *x2,int *y2)
{
   *x1 = xco1; *y1 = yco1;
   *x2 = xco2; *y2 = yco2;
}

/* Stop marking it on the map, for when the section window is closed. */
void cross_section_forget(void)
{
   flagv = 0;
   flagl = 0;
   draw_map(1);
}

/* Draw the cut over the map, so that it stays visible while the section window
 * is open and survives a pan, a zoom or the next file.  The endpoints are held
 * in grid cells for that reason; this is the inverse of surface_to_map(), with
 * the same origin, and it runs in the map context, whose origin is the map's
 * top left rather than the surface's. */
static void draw_cross_line(void)
{
   int x_begin,y_begin,x_start,y_start;
   int x1,y1,x2,y2;

   if (!flagv) return;

   map_origin(&x_begin,&y_begin,&x_start,&y_start);
   x1=x_start+(xco1*2-x_begin)*MPIX;
   y1=y_start+(yco1*2-y_begin)*MPIX;
   x2=x_start+(xco2*2-x_begin)*MPIX;
   y2=y_start+(yco2*2-y_begin)*MPIX;

   GrLine(x1,y1,x2,y2,colors[15]);
   GrLine(x1+1,y1,x2+1,y2,colors[15]);
   GrLine(x1,y1+1,x2,y2+1,colors[15]);
   GrCircle(x1,y1,4,colors[15]);
   GrCircle(x2,y2,4,colors[15]);
}
#endif /* QTGUI */
#endif

int init_graph(char *palette_file) {
int i;
int svga;
int bw,bh;                /* the batch surface, see the #else branch below */
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
                  qt_surface_width(),qt_surface_height(),256*256*256L)) {
      fprintf(logfile,"Can't set a %dx%d truecolor memory mode\n",
              qt_surface_width(),qt_surface_height());
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
   /* The batch surface is only as wide as the legend column, because that is
    * all the batch path draws on it - the map goes to RamContext.  A vertical
    * section does not: vert() paints across the whole window, out to
    * LeftX+MaxHorizLine, so cross= needs room for it. */
   bw=batch_cross ? WINDOW_LEFT+WINDOW_XSIZE+8 : WINDOW_LEFT;
   bh=WINDOW_UP+WINDOW_YSIZE+(batch_cross?24:0);
   if (!GrSetMode(GR_width_height_color_graphics,bw,bh,256*256*256L)) {
      fprintf(logfile,"Can't set a %dx%d truecolor memory mode\n",bw,bh);
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
#if defined(GUI)
   /* The map is drawn into RAM and blitted to the window.  GrBitBltNC() only
    * has a transfer routine when the source frame mode is the one the screen
    * driver names as its compatible RAM mode - otherwise it falls off the end
    * of its if/else and returns, drawing nothing at all.  GR_frameRAM24 is
    * that mode only on a 24 bit X visual; a depth 24 screen actually gives
    * XWIN32L/RAM32L and a depth 16 one XWIN16/RAM16, which is why blitting
    * appeared not to work here and got replaced by a PNG round trip through
    * /tmp.  Ask the driver instead of guessing. */
   RamContext=GrCreateFrameContext(GrCoreFrameMode(),WINDOW_XSIZE,WINDOW_YSIZE,NULL,NULL);
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
#ifdef QTGUI
draw_cross_line();      /* where the section window is cutting, if anywhere */
#endif

}   /* MRES>0 */


#if defined(QTGUI)
qt_compose_map();   // put the map on the screen surface Qt shows
#elif defined(GUI)
GrBitBlt(&ScreenContext,WINDOW_LEFT,WINDOW_UP,
         RamContext,0,0,WINDOW_XSIZE-1,WINDOW_YSIZE-1,GrWRITE);
#endif

GrSetContext(&ScreenContext);

GrBox(WINDOW_LEFT-1,WINDOW_UP-1,WINDOW_LEFT+WINDOW_XSIZE,WINDOW_UP+WINDOW_YSIZE,GrAllocColor(255,255,255));

show_header();
#if defined(QTGUI)
/* Lets animate() repaint and stay interruptible: space stops it.  Only while
 * it is running, though - polling here consumes a pending keypress and every
 * caller but animate() throws the result away, so a key struck during any
 * other repaint was simply lost.  The reload after an animation stops is slow
 * enough to swallow the next space that way.  message_poll() also drags the
 * cursor readout along with it, which is a repaint nobody asked for. */
return anim_running ? qt_poll_key() : 0;
#elif defined(GUI)
return anim_running ? message_poll() : 0;
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
