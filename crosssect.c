/* crosssect.c - vertical cross section through the constant altitude products.
 *
 * vert.c draws the section the viewer has always had, and imagegcc still uses
 * it: the nearest cell along a Bresenham line, one row per quarter kilometre,
 * and the gaps between the levels filled by ramping the palette bytes.  It
 * writes straight onto the map window, so the map has to be reloaded to get
 * rid of it again.
 *
 * This is the other one.  It computes physical values and hands them to the
 * caller; the Qt front end shows them in a window of its own and the map is
 * left alone.  The samples are interpolated in three dimensions - bilinear
 * across the map grid, linear in altitude between the two levels bracketing
 * the point, and in the quantity rather than in palette bytes: reflectivity
 * is averaged as linear Z and differential reflectivity as the power ratio it
 * is the logarithm of, so a 20 dBZ cell next to a 50 dBZ one no longer
 * averages to 35.
 *
 * The scheme is the one FormRLSAir1::viewClipPlane() uses in uvknew, down to
 * the two rules that keep it honest:
 *
 *   - a bilinear cell whose four corners are not all readings falls back to
 *     the nearest corner, so the edge of an echo is not blended into the empty
 *     grid around it;
 *   - between two levels, one of which has no reading, the sample takes the
 *     other level's value only while that level is the nearer of the two, and
 *     is empty beyond.  Ramping to nothing instead is what makes a smoothly
 *     interpolated echo top climb half a kilometre higher than it is.
 *
 * Nothing here draws, so it builds into gen-bitmap and imagegcc as well; only
 * the Qt front end calls it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "image.h"
#include "qt_bridge.h"

/* The lowest elevation angle, per port, in radians.  masx/masy (the radar's
 * position on the map grid) and HMRL (its altitude) are in image.h; this one
 * only vert.c ever wanted, so it never made it there. */
extern float naa[MaxPorts];

#define CS_MAX_LEVELS 32
#define CS_MIN_WIDTH  8
#define CS_MAX_WIDTH  4096
#define CS_VRES       0.05f   /* km per sample row */
#define CS_EARTH      8500.0  /* 4/3 earth radius, km - vert.c calls it Ref */

/* "this sample carries a reading".  Kept as a test rather than an equality so
 * that arithmetic on the marker cannot accidentally produce a value. */
#define CS_HAS(v)  ((v) > -9000.0f)

struct cs_level {
   int   map;    /* index into maps[] */
   float km;     /* altitude of the level */
};

/* ------------------------------------------------------------------ */
/* the levels                                                          */
/* ------------------------------------------------------------------ */

/* Every level of one family the file on screen carries, lowest first.  Level 0
 * is excluded on purpose: for reflectivity that is the column maximum, which
 * belongs to no altitude. */
static int collect_levels(int family,struct cs_level *level,int max)
{
   int i,j,n=0;
   float km;

   for (i=0;i<no_maps && n<max;i++) {
      if ((int)maps[i].family!=family) continue;
      if (maps[i].level==0) continue;
      if (maps[i].mapres==0 || maps[i].bufdata==NULL) continue;
      if (maps[i].bufhead==NULL) continue;
      km=maps[i].bufhead[0]/10.0f;              /* the passport, not level */
      if (km<=0) continue;
      level[n].map=i;
      level[n].km=km;
      n++;
   }

   /* the products arrive in table order, which is not promised to be altitude
    * order, and everything below reads them as a sorted stack */
   for (i=1;i<n;i++) {
      struct cs_level hold=level[i];
      for (j=i;j>0 && level[j-1].km>hold.km;j--) level[j]=level[j-1];
      level[j]=hold;
   }

   /* two products at one altitude would leave a zero thick layer to
    * interpolate across, and the weight for it is a division by it */
   for (i=1;i<n;i++)
      if (level[i].km-level[i-1].km<0.01f) {
         memmove(level+i,level+i+1,(n-i-1)*sizeof(*level));
         n--;
         i--;
      }
   return n;
}

int cross_section_levels(int family)
{
   struct cs_level level[CS_MAX_LEVELS];

   return collect_levels(family,level,CS_MAX_LEVELS);
}

/* The families worth cutting through: two levels at least, or there is
 * nothing to interpolate between. */
int cross_section_families(int *families,int max)
{
   static const int order[]={ FAM_DBZ,FAM_ZDR,FAM_VEL };
   int i,n=0;

   for (i=0;i<(int)(sizeof(order)/sizeof(order[0])) && n<max;i++)
      if (cross_section_levels(order[i])>=2) families[n++]=order[i];
   return n;
}

/* ------------------------------------------------------------------ */
/* the byte scales                                                     */
/* ------------------------------------------------------------------ */

/* Byte to physical value, in the same units and on the same scales the cursor
 * readout prints (format_reading() in showdata.c).  The two have to agree:
 * reading a point off the map and off the section must give one answer. */
static float cs_value(int family,int byte)
{
   switch (family) {
   case FAM_DBZ: return byte/3.0f;
   case FAM_ZDR:  return zdr_value(byte);       /* the scale wraps - palette.c */
   case FAM_VEL: return (byte-127)/2.0f;
   default:      return (float)byte;
   }
}

/* ... and back, which is how an interpolated value gets a colour.  There is no
 * palette of physical values: the .pal files are indexed by the .wrk byte. */
int cross_section_byte(int family,float value)
{
   int byte;

   if (!CS_HAS(value)) return -1;

   /* The clamps keep an interpolated value off the two bytes that are not
    * readings.  A sample that lands on the no-data byte would be a hole in the
    * middle of an echo, and one that lands on "no echo" would be a hole that
    * is also the wrong colour. */
   switch (family) {
   case FAM_DBZ:
      byte=(int)floor(value*3.0+0.5);
      if (byte<1) byte=1;                       /* 0 is "no echo" */
      if (byte>253) byte=253;                   /* 254 is the no-data byte */
      break;
   case FAM_VEL:
      byte=(int)floor(value*2.0+0.5)+127;
      if (byte<1) byte=1;
      if (byte>254) byte=254;                   /* 255 is the no-data byte */
      break;
   case FAM_ZDR:
      byte=zdr_byte(value);                     /* the inverse, in palette.c */
      break;
   default:
      byte=(int)floor(value+0.5);
      if (byte<0) byte=0;
      if (byte>255) byte=255;
      break;
   }
   return byte;
}

/* The domain to average in.  Reflectivity is a logarithm of a power and so is
 * differential reflectivity, so both are taken back to the power before they
 * are weighted; a velocity is a velocity. */
static double to_linear(int family,float value)
{
   if (family==FAM_DBZ || family==FAM_ZDR) return pow(10.0,value/10.0);
   return value;
}

static float from_linear(int family,double value)
{
   if (family==FAM_DBZ || family==FAM_ZDR)
      return value>0.0 ? (float)(10.0*log10(value)) : CS_NODATA;
   return (float)value;
}

/* ------------------------------------------------------------------ */
/* sampling one level                                                  */
/* ------------------------------------------------------------------ */

static int cell(const struct map_info *map,int x,int y)
{
   if (x<0 || y<0 || x>=(int)MSIZE_int || y>=(int)MSIZE_int) return -1;
   return map->bufdata[(long)y*MSIZE_int+x];
}

/* A byte with nothing in it.  "No echo" counts: it is a reading that the beam
 * swept and found nothing, and blending it with a real one would paint a
 * gradient across an echo edge that is a step. */
static int empty(const struct map_info *map,int byte)
{
   if (byte<0) return 1;
   if (byte==map->nodata) return 1;
   if (map->noecho>=0 && byte==map->noecho) return 1;
   return 0;
}

static float nearest(const struct map_info *map,int family,float fx,float fy)
{
   int byte=cell(map,(int)floor(fx+0.5),(int)floor(fy+0.5));

   return empty(map,byte) ? CS_NODATA : cs_value(family,byte);
}

/* Bilinear across the grid, in the physical quantity.  Any corner without a
 * reading and the whole cell falls back to the nearest one - see the header. */
static float sample_level(const struct map_info *map,int family,
                          float fx,float fy,int smooth)
{
   int x0,y0,i,byte[4];
   float wx,wy,weight[4];
   double lin=0.0;

   if (!smooth) return nearest(map,family,fx,fy);

   x0=(int)floor(fx);
   y0=(int)floor(fy);
   wx=fx-x0;
   wy=fy-y0;

   byte[0]=cell(map,x0,  y0);    weight[0]=(1-wx)*(1-wy);
   byte[1]=cell(map,x0+1,y0);    weight[1]=wx*(1-wy);
   byte[2]=cell(map,x0+1,y0+1);  weight[2]=wx*wy;
   byte[3]=cell(map,x0,  y0+1);  weight[3]=(1-wx)*wy;

   for (i=0;i<4;i++) if (empty(map,byte[i])) return nearest(map,family,fx,fy);

   for (i=0;i<4;i++) lin+=weight[i]*to_linear(family,cs_value(family,byte[i]));
   return from_linear(family,lin);
}

/* Between two levels.  `upper` is the weight of the level above, so 0 is the
 * lower level's own altitude and 1 the upper one's. */
static float blend(int family,float below,float above,float upper)
{
   double lin;

   if (!CS_HAS(below) && !CS_HAS(above)) return CS_NODATA;
   if (!CS_HAS(below)) return upper>0.5f ? above : CS_NODATA;
   if (!CS_HAS(above)) return upper<0.5f ? below : CS_NODATA;

   lin=(1.0-upper)*to_linear(family,below)+upper*to_linear(family,above);
   return from_linear(family,lin);
}

/* ------------------------------------------------------------------ */
/* the beam floor                                                      */
/* ------------------------------------------------------------------ */

/* How high the lowest beam of the nearest radar is over a point: below this
 * the volume was never scanned, and an empty sample there means "not looked
 * at" rather than "nothing there".  Same formula vert.c uses, over a 4/3
 * earth, but ports that sent nothing are skipped - their position is still
 * (0,0), the corner of the map, and vert.c lets one of those win. */
static float beam_floor(float fx,float fy,float res)
{
   int port,best=-1;
   double distance,closest=1e30,km;

   for (port=0;port<MaxPorts;port++) {
      if (masx[port]==0 && masy[port]==0) continue;
      distance=hypot(fx-masx[port],fy-masy[port]);
      if (distance<closest) { closest=distance; best=port; }
   }
   if (best<0) return 0.0f;

   km=closest*res;
   return (float)(km*km/(2*CS_EARTH)+km*sin(naa[best])+HMRL[best]);
}

/* ------------------------------------------------------------------ */
/* the section                                                         */
/* ------------------------------------------------------------------ */

void cross_section_release(struct cross_section *cs)
{
   if (cs==NULL) return;
   free(cs->value);
   free(cs->floor_km);
   free(cs->level_km);
   memset(cs,0,sizeof(*cs));
}

/* x1,y1 - x2,y2 are cells of the product grid, which is what
 * cross_section_endpoints() reports and what vert() takes.  Returns 0 when
 * there is nothing to draw: too few levels, or too short a line. */
int cross_section_compute(int x1,int y1,int x2,int y2,int family,int smooth,
                          struct cross_section *cs)
{
   struct cs_level level[CS_MAX_LEVELS];
   float column[CS_MAX_LEVELS];
   int levels,width,height,ix,iz,k;
   float res,length,fx,fy,z,top;

   memset(cs,0,sizeof(*cs));

   levels=collect_levels(family,level,CS_MAX_LEVELS);
   if (levels<2) return 0;

   res=maps[level[0].map].mapres;               /* km per grid cell */
   if (res<=0) return 0;

   length=(float)hypot(x2-x1,y2-y1);
   if (length<2.0f) return 0;

   /* one sample per half cell, the resolution the map itself is drawn at
    * (mapbuffer is the product grid expanded by two) */
   width=(int)(length*2.0f)+1;
   if (width<CS_MIN_WIDTH) width=CS_MIN_WIDTH;
   if (width>CS_MAX_WIDTH) width=CS_MAX_WIDTH;

   top=level[levels-1].km;
   height=(int)(top/CS_VRES)+1;
   if (height<2) return 0;

   cs->value=malloc((size_t)width*height*sizeof(float));
   cs->floor_km=malloc((size_t)width*sizeof(float));
   cs->level_km=malloc((size_t)levels*sizeof(float));
   if (cs->value==NULL || cs->floor_km==NULL || cs->level_km==NULL) {
      cross_section_release(cs);
      return 0;
   }

   cs->width=width;
   cs->height=height;
   cs->levels=levels;
   cs->family=family;
   cs->length_km=length*res;
   cs->top_km=top;
   cs->base_km=level[0].km;
   cs->smooth=smooth;
   for (k=0;k<levels;k++) cs->level_km[k]=level[k].km;

   for (ix=0;ix<width;ix++) {
      float t=width>1 ? (float)ix/(width-1) : 0.0f;

      fx=x1+t*(x2-x1);
      fy=y1+t*(y2-y1);

      for (k=0;k<levels;k++)
         column[k]=sample_level(&maps[level[k].map],family,fx,fy,smooth);

      cs->floor_km[ix]=beam_floor(fx,fy,res);

      k=0;
      for (iz=0;iz<height;iz++) {
         float *out=cs->value+(size_t)iz*width+ix;
         float upper;

         z=iz*top/(height-1);

         /* nothing is invented outside the stack: below the lowest level and
          * above the highest the section is empty */
         if (z<level[0].km || z>level[levels-1].km) { *out=CS_NODATA; continue; }

         while (k<levels-2 && z>level[k+1].km) k++;
         while (k>0 && z<level[k].km) k--;

         upper=(z-level[k].km)/(level[k+1].km-level[k].km);
         if (upper<0.0f) upper=0.0f;
         if (upper>1.0f) upper=1.0f;

         if (!smooth) *out=upper<0.5f ? column[k] : column[k+1];
         else         *out=blend(family,column[k],column[k+1],upper);
      }
   }
   return 1;
}

/* ------------------------------------------------------------------ */
/* colours, for a front end that cannot include image.h                */
/* ------------------------------------------------------------------ */

/* The palette a family is drawn with, and the map whose no-data byte overrides
 * it - the same pairing set_palette() is called with for the map window. */
static int family_map(int family)
{
   int i;

   for (i=0;i<no_maps;i++)
      if ((int)maps[i].family==family && maps[i].level!=0) return i;
   for (i=0;i<no_maps;i++)
      if ((int)maps[i].family==family) return i;
   return -1;
}

static struct palette *family_palette(int family)
{
   static struct palette loaded;
   static char have[24]="";
   int map=family_map(family);

   if (map<0) return NULL;
   if (!strcmp(have,maps[map].palette)) return &loaded;

   if (!load_palette(grfdir,maps[map].palette,&loaded)) {
      have[0]=0;
      return NULL;
   }
   strncpy(have,maps[map].palette,sizeof(have)-1);
   have[sizeof(have)-1]=0;

   /* the product's own marker wins over the palette's, as on the map */
   loaded.nodata=maps[map].nodata;
   return &loaded;
}

int cross_section_colors(int family,unsigned char *rgb)
{
   struct palette *pal=family_palette(family);
   int i;

   if (pal==NULL) return 0;
   for (i=0;i<256;i++) {
      rgb[i*3+0]=pal->rgb[i][0];
      rgb[i*3+1]=pal->rgb[i][1];
      rgb[i*3+2]=pal->rgb[i][2];
   }
   return 1;
}

/* The legend, one row at a time, strongest band first.  The label is cp866,
 * as everything else this program writes is. */
int cross_section_legend(int family,int row,unsigned char *rgb,
                         char *label,int size)
{
   struct palette *pal=family_palette(family);

   if (pal==NULL || row<0 || row>=pal->rows) return 0;
   rgb[0]=pal->row[row].rgb[0];
   rgb[1]=pal->row[row].rgb[1];
   rgb[2]=pal->row[row].rgb[2];
   if (size>0) {
      strncpy(label,pal->row[row].label,size-1);
      label[size-1]=0;
   }
   return 1;
}

const char *cross_section_units(int family)
{
   struct palette *pal=family_palette(family);

   return pal!=NULL ? pal->units : "";
}

/* The cp866 title of the family, for the window to put over its legend. */
const char *cross_section_title(int family)
{
   int map=family_map(family);

   return map>=0 ? maps[map].descr : "";
}

/* ------------------------------------------------------------------ */
/* the section as a picture                                            */
/* ------------------------------------------------------------------ */

/* The section as a raster of palette bytes, for a caller that wants a picture
 * rather than the numbers - pyimage.py writes a PNG straight from this, and a
 * web page is the reason it exists.  Row 0 is the TOP, the way an image is
 * stored and not the way the section is computed; one byte per cell, to be
 * looked up in the table cross_section_colors() fills.  A cell the beam never
 * reached carries the product's own no-data byte, so it can be made
 * transparent by whoever writes the file.
 *
 * Call it with out=NULL to learn the size, allocate width*height, and call it
 * again.  Returns 1 when there is a section to draw and 0 when there is not:
 * fewer than two levels of the family, or a line too short to cut along. */
int cross_section_raster(int x1,int y1,int x2,int y2,int family,int smooth,
                         unsigned char *out,int max,int *width,int *height,
                         float *length_km,float *top_km,float *base_km)
{
   struct cross_section cs;
   struct palette *pal;
   int ix,iz,byte,empty;

   if (!cross_section_compute(x1,y1,x2,y2,family,smooth,&cs)) return 0;

   if (width)     *width     = cs.width;
   if (height)    *height    = cs.height;
   if (length_km) *length_km = cs.length_km;
   if (top_km)    *top_km    = cs.top_km;
   if (base_km)   *base_km   = cs.base_km;

   if (out==NULL || max<cs.width*cs.height) {
      cross_section_release(&cs);
      return out==NULL;                         /* sizing call, or too small */
   }

   pal=family_palette(family);
   empty = pal!=NULL ? pal->nodata : 0;

   for (iz=0;iz<cs.height;iz++) {
      const float *src=cs.value+(long)iz*cs.width;
      unsigned char *dst=out+(long)(cs.height-1-iz)*cs.width;

      for (ix=0;ix<cs.width;ix++) {
         byte=cross_section_byte(family,src[ix]);
         dst[ix]= byte<0 ? (unsigned char)empty : (unsigned char)byte;
      }
   }
   cross_section_release(&cs);
   return 1;
}
