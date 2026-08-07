/* geotiff.c - write the composite grid as a georeferenced TIFF.
 *
 * gen-bitmap normally saves a picture: the map as it is drawn, geography and
 * legend on top, in screen pixels.  That is no use as data.  This writes the
 * mosaic itself instead - one byte per grid cell, the palette as the TIFF
 * colour map, and enough georeferencing for GDAL to place it - which is what
 * radar-wms/geotiff.py produces from the Python side, without going through
 * Python at all.
 *
 * The grid is written in the projection it is already built in (see
 * krass_projection in coord.c), so nothing is resampled and no value changes.
 * A consumer that wants the pycao frame can reproject:
 *
 *     gdalwarp -t_srs '+proj=sterea +lat_0=50 +lon_0=100' -r near in.tif out.tif
 *
 * -r near matters: these are palette indices, not intensities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tiffio.h>
#include "image.h"

/* libtiff dropped these from its public headers; the field table wants them */
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* GeoTIFF lives in private TIFF tags.  libtiff needs to be told they exist
 * before it will let us write them. */
#define TIFFTAG_GEOPIXELSCALE   33550
#define TIFFTAG_GEOTIEPOINTS    33922
#define TIFFTAG_GEOKEYDIRECTORY 34735
#define TIFFTAG_GEODOUBLEPARAMS 34736
#define TIFFTAG_GEOASCIIPARAMS  34737
#define TIFFTAG_GDAL_NODATA     42113

static const TIFFFieldInfo geo_fields[] = {
  { TIFFTAG_GEOPIXELSCALE,  -1,-1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, "GeoPixelScale" },
  { TIFFTAG_GEOTIEPOINTS,   -1,-1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, "GeoTiePoints" },
  { TIFFTAG_GEOKEYDIRECTORY,-1,-1, TIFF_SHORT,  FIELD_CUSTOM, TRUE, TRUE, "GeoKeyDirectory" },
  { TIFFTAG_GEODOUBLEPARAMS,-1,-1, TIFF_DOUBLE, FIELD_CUSTOM, TRUE, TRUE, "GeoDoubleParams" },
  { TIFFTAG_GEOASCIIPARAMS, -1,-1, TIFF_ASCII,  FIELD_CUSTOM, TRUE, FALSE,"GeoASCIIParams" },
  { TIFFTAG_GDAL_NODATA,    -1,-1, TIFF_ASCII,  FIELD_CUSTOM, TRUE, FALSE,"GDALNoDataValue" }
};

static TIFFExtendProc parent_extender = NULL;

static void geo_extender(TIFF *tif)
{
   TIFFMergeFieldInfo(tif, geo_fields, sizeof(geo_fields)/sizeof(geo_fields[0]));
   if (parent_extender) (*parent_extender)(tif);
}

static void register_geo_tags(void)
{
   static int done = 0;
   if (done) return;
   parent_extender = TIFFSetTagExtender(geo_extender);
   done = 1;
}

/* GeoTIFF key codes, from the specification's appendix. */
#define GTModelTypeGeoKey        1024
#define GTRasterTypeGeoKey       1025
#define GTCitationGeoKey         1026
#define GeographicTypeGeoKey     2048
#define ProjectedCSTypeGeoKey    3072
#define PCSCitationGeoKey        3073
#define ProjectionGeoKey         3074
#define ProjCoordTransGeoKey     3075
#define ProjLinearUnitsGeoKey    3076
#define ProjStdParallel1GeoKey   3078
#define ProjStdParallel2GeoKey   3079
#define ProjFalseEastingGeoKey   3082
#define ProjFalseNorthingGeoKey  3083
#define ProjCenterLongGeoKey     3088
#define ProjCenterLatGeoKey      3089

#define ModelTypeProjected       1
#define RasterPixelIsArea        1
#define GCS_WGS_84               4326
#define Linear_Meter             9001
#define CT_EquidistantConic      13
#define KvUserDefined            32767

/* One entry of the key directory: key, where the value lives, count, value. */
static void put_key(unsigned short *d,int *n,unsigned short key,
                    unsigned short loc,unsigned short count,unsigned short val)
{
   d[(*n)*4+0]=key; d[(*n)*4+1]=loc; d[(*n)*4+2]=count; d[(*n)*4+3]=val;
   (*n)++;
}

/*
 * grid       size*size bytes, row 0 at the north edge
 * west,north top left corner of the raster, in projected metres
 * pixel_m    cell size in metres
 * lat0,lon0  the projection's origin, in degrees
 * sp1,sp2    its standard parallels
 */
int write_geotiff(const char *path,const unsigned char *grid,int size,
                  double west,double north,double pixel_m,
                  double lat0,double lon0,double sp1,double sp2,
                  const struct palette *pal,int nodata)
{
   TIFF *tif;
   unsigned short *red,*green,*blue;
   unsigned short keys[80];
   double dparams[8];
   double tiepoint[6],pixscale[3];
   char citation[200],nodatatext[16];
   int nkeys=0,ndoubles=0,row,i;

   register_geo_tags();

   if ((tif=TIFFOpen(path,"w"))==NULL) {
      fprintf(stderr,"cannot write %s\n",path);
      return 0;
   }

   TIFFSetField(tif,TIFFTAG_IMAGEWIDTH,(unsigned)size);
   TIFFSetField(tif,TIFFTAG_IMAGELENGTH,(unsigned)size);
   TIFFSetField(tif,TIFFTAG_BITSPERSAMPLE,8);
   TIFFSetField(tif,TIFFTAG_SAMPLESPERPIXEL,1);
   TIFFSetField(tif,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
   TIFFSetField(tif,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_PALETTE);
   TIFFSetField(tif,TIFFTAG_COMPRESSION,COMPRESSION_ADOBE_DEFLATE);
   TIFFSetField(tif,TIFFTAG_ROWSPERSTRIP,TIFFDefaultStripSize(tif,0));

   /* the palette, as TIFF wants it: three 16 bit ramps */
   red  =calloc(256,sizeof(unsigned short));
   green=calloc(256,sizeof(unsigned short));
   blue =calloc(256,sizeof(unsigned short));
   if (red==NULL || green==NULL || blue==NULL) {
      fprintf(stderr,"out of memory writing %s\n",path);
      free(red); free(green); free(blue); TIFFClose(tif);
      return 0;
   }
   for (i=0;i<256;i++) {
      red[i]  =(unsigned short)(pal->rgb[i][0]*257);
      green[i]=(unsigned short)(pal->rgb[i][1]*257);
      blue[i] =(unsigned short)(pal->rgb[i][2]*257);
   }
   TIFFSetField(tif,TIFFTAG_COLORMAP,red,green,blue);

   sprintf(nodatatext,"%d",nodata);
   TIFFSetField(tif,TIFFTAG_GDAL_NODATA,nodatatext);

   /* Where the raster sits: one tie point mapping raster (0,0) - the top left
    * corner, because the raster type is PixelIsArea - to projected metres. */
   tiepoint[0]=0.0; tiepoint[1]=0.0; tiepoint[2]=0.0;
   tiepoint[3]=west; tiepoint[4]=north; tiepoint[5]=0.0;
   pixscale[0]=pixel_m; pixscale[1]=pixel_m; pixscale[2]=0.0;
   TIFFSetField(tif,TIFFTAG_GEOTIEPOINTS,6,tiepoint);
   TIFFSetField(tif,TIFFTAG_GEOPIXELSCALE,3,pixscale);

   /* The projection, as a user defined equidistant conic.  The citation is
    * what gdalinfo shows as the CRS name. */
   sprintf(citation,"IMAGE radar composite: +proj=eqdc +lat_1=%g +lat_2=%g "
                    "+lat_0=%g +lon_0=%g +x_0=0 +y_0=0 +datum=WGS84 +units=m",
           sp1,sp2,lat0,lon0);

   put_key(keys,&nkeys,GTModelTypeGeoKey,0,1,ModelTypeProjected);
   put_key(keys,&nkeys,GTRasterTypeGeoKey,0,1,RasterPixelIsArea);
   put_key(keys,&nkeys,GTCitationGeoKey,TIFFTAG_GEOASCIIPARAMS,
           (unsigned short)(strlen(citation)+1),0);
   put_key(keys,&nkeys,GeographicTypeGeoKey,0,1,GCS_WGS_84);
   put_key(keys,&nkeys,ProjectedCSTypeGeoKey,0,1,KvUserDefined);
   put_key(keys,&nkeys,PCSCitationGeoKey,TIFFTAG_GEOASCIIPARAMS,
           (unsigned short)(strlen(citation)+1),0);
   put_key(keys,&nkeys,ProjectionGeoKey,0,1,KvUserDefined);
   put_key(keys,&nkeys,ProjCoordTransGeoKey,0,1,CT_EquidistantConic);
   put_key(keys,&nkeys,ProjLinearUnitsGeoKey,0,1,Linear_Meter);

   dparams[ndoubles]=sp1;
   put_key(keys,&nkeys,ProjStdParallel1GeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;
   dparams[ndoubles]=sp2;
   put_key(keys,&nkeys,ProjStdParallel2GeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;
   dparams[ndoubles]=lat0;
   put_key(keys,&nkeys,ProjCenterLatGeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;
   dparams[ndoubles]=lon0;
   put_key(keys,&nkeys,ProjCenterLongGeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;
   dparams[ndoubles]=0.0;
   put_key(keys,&nkeys,ProjFalseEastingGeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;
   dparams[ndoubles]=0.0;
   put_key(keys,&nkeys,ProjFalseNorthingGeoKey,TIFFTAG_GEODOUBLEPARAMS,1,ndoubles); ndoubles++;

   /* the directory header counts the keys that follow it */
   {
      unsigned short dir[4+4*20];
      dir[0]=1; dir[1]=1; dir[2]=0;      /* version 1.1.0 */
      dir[3]=(unsigned short)nkeys;
      memcpy(dir+4,keys,nkeys*4*sizeof(unsigned short));
      TIFFSetField(tif,TIFFTAG_GEOKEYDIRECTORY,(nkeys+1)*4,dir);
   }
   TIFFSetField(tif,TIFFTAG_GEODOUBLEPARAMS,ndoubles,dparams);
   TIFFSetField(tif,TIFFTAG_GEOASCIIPARAMS,citation);

   for (row=0;row<size;row++)
      if (TIFFWriteScanline(tif,(void *)(grid+(long)row*size),row,0)<0) {
         fprintf(stderr,"error writing row %d of %s\n",row,path);
         free(red); free(green); free(blue); TIFFClose(tif);
         return 0;
      }

   TIFFClose(tif);
   free(red); free(green); free(blue);
   return 1;
}

/* Save the product currently selected.  The grid is maps[].bufdata: one cell
 * per mapres kilometres, MSIZE_int of them each way, centred on the projection
 * origin - which is where get_ptr() puts it. */
int save_geotiff(const char *path)
{
   double cell_m,half_m;

   static struct palette pal;
   int nodata=maps[current_map].nodata;

   if (maps[current_map].bufdata==NULL || maps[current_map].mapres==0) {
      fprintf(stderr,"no data for this product, not writing %s\n",path);
      return 0;
   }
   /* load_palette() rather than set_palette(): the colour map goes into the
    * file, so none of this needs a graphics context. */
   if (!load_palette(grfdir,maps[current_map].palette,&pal)) return 0;

   /* The palette was generated for one no-data byte and the map may use
    * another; paint whichever this product uses as no data.  The generator
    * always puts that row last. */
   if (pal.rows>0 && nodata>=0 && nodata<256)
      memcpy(pal.rgb[nodata],pal.row[pal.rows-1].rgb,3);

   cell_m=maps[current_map].mapres*1000.0;
   half_m=cell_m*(double)MSIZE_int/2.0;

   return write_geotiff(path,maps[current_map].bufdata,MSIZE_int,
                        -half_m,half_m,cell_m,
                        BU*GR,LU*GR,47.0,62.0,
                        &pal,nodata);
}
