
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

#include "minizip_compat.h"
#include "image.h"

#ifdef __TURBOC__
# include <malloc.h>
#endif

typedef struct _UzpFilesBuffer {
    char  *fname;             /* file name */
    unsigned long   strlength;          /* length of string  */
    char  *strptr;            /* pointer to string */
} UzpFilesBuffer;



void  delay(int a) {
//placehoolder
}

char head_wrk[]="header.wrk";

char grfdir[80];
char cfgdir[80];
char mapdir[80];
char in_dir[MaxPorts][100];
char s[80];
char ST[MaxPorts][50];
int cur,fp=0;

char current_prefix='4';
int current_map=0;
float MRES;  /* current map resolution */
unsigned char _HUGE *mapbuffer;

unsigned char header[128];

float LU,BU,LC,BC;
int DX[3],SY[3];
int height;           /* высот  центр  сбор  в метр х */
unsigned int MSIZE;	      /* р змер к рты */
unsigned int MSIZE_int;
float SPEED[MaxPorts];
float W[MaxPorts][MaxNoMaps];
signed int AZIMUT[MaxPorts];
float           bw,bww,tetu[MaxPorts],naa[MaxPorts];
int CORR[MaxPorts];
int R[MaxPorts];
int num;
int cod;

float BW;
float HMRL[MaxPorts];
float L0[MaxPorts],B0[MaxPorts];
char center_name[15];
int fk[MaxNoMaps][MaxPorts];


FILE *pat;
int flags=0;
int masx[MaxPorts], masy[MaxPorts];

/* Every product bufr2wrk.py writes.  The table is built rather than spelled
 * out: the reflectivity, differential reflectivity and velocity levels differ
 * only in their number, and there are 35 of them between the three.
 *
 * `nodata` and `noecho` are the bytes that product uses for a point it has no
 * reading for and for one the beam swept without finding anything.  `nodata`
 * must agree with the .pal file mkpalettes.py writes for the same product.
 *
 * The rain rate has two no-data bytes.  bufr2wrk.py gives it no marker of its
 * own - unlike reflectivity (254), ZDR (121) or velocity (255) - so a point
 * with no reading comes out as whatever the top of the run length table
 * saturates to, and that depends on how wide the source field is: 221 for the
 * DMRL radars, 164 for the AKSOPRI ones (184 of 210 archives sampled).  The
 * second one is remapped onto the first as the file is read, so everything
 * downstream sees a single marker.
 * It is not the same everywhere - bufr2wrk gives radial velocity a dedicated
 * 255 and differential reflectivity inherits 121 from debufr's wrapped value
 * table - so it has to travel with the map rather than be assumed to be 254.
 */
struct map_info maps[MaxNoMaps];
int no_maps;

static char name_of[MaxNoMaps][16];
static char descr_of[MaxNoMaps][16];

static void add_map(map_type id,char *file,char *palette,char *title,
                    product_family family,int level,int nodata,
                    int nodata_alt1,int nodata_alt2,int noecho,int merge)
{
   struct map_info *map;

   if (no_maps>=MaxNoMaps) return;
   map=&maps[no_maps];
   strncpy(name_of[no_maps],file,sizeof(name_of[0])-1);
   strncpy(descr_of[no_maps],title,sizeof(descr_of[0])-1);
   map->mapid=id;
   map->filename=name_of[no_maps];
   map->palette=palette;
   map->descr=descr_of[no_maps];
   map->family=family;
   map->level=level;
   map->nodata=nodata;
   map->nodata_alt[0]=nodata_alt1;
   map->nodata_alt[1]=nodata_alt2;
   map->noecho=noecho;
   map->merge=merge;
   map->mapres=0;
   map->bufhead=NULL;
   map->bufdata=NULL;
   no_maps++;
}

void init_maps(void)
{
   static int sum_hours[5]={1,3,6,12,24};
   char file[24],title[16];
   int i;

   no_maps=0;

   /* 4_dbz_0.wrk read two ways: as the rain rate product it is, and as the
    * maximum reflectivity it was computed from.  Both readings share the
    * file's two no-data bytes - 221 is 1431 mm/h and 164 is 93 mm/h on the
    * rain scale, 73 and 55 dBZ on the reflectivity one, and painting either
    * as a reading fills the whole out-of-range area with a downpour. */
   /* bufr2wrk.py writes 255 for a point with no reading.  Before it did, the
    * run length table just saturated: 221 (1431 mm/h) from bufr2wrk itself,
    * and 164 from debufr.exe before the pipeline changed over in July 2026.
    * Both are folded onto 255 as the file is read. */
   add_map(MAP_P,"dbz_0.wrk","precip_rate","Инт. мм/ч",FAM_RAIN,0,255,221,164,0,MERGE_IDW);
   add_map(MAP_0,"dbz_0.wrk","reflectivity","Отраж макс",FAM_DBZ,0,255,221,164,0,MERGE_IDW);
   for (i=1;i<=15;i++) {
      sprintf(file,"dbz_%d.wrk",i);
      sprintf(title,"Отраж %d",i);
      add_map(MAP_1+i-1,file,"reflectivity",title,FAM_DBZ,i,254,-1,-1,0,MERGE_IDW);
   }

   add_map(MAP_H,"heigh.wrk","height","Высота ВГО",FAM_HEIGHT,0,254,-1,-1,0,MERGE_MAX);
   /* 4_myavl.wrk, not 4_storm.wrk: it stores the phenomena code itself, which
    * is what the pycao phenomena palette is keyed on.  storm.wrk carries the
    * old 0/3/5/../60 severity scale, which that palette cannot read. */
   add_map(MAP_S,"myavl.wrk","phenomena","Опасн явл",FAM_PHENOM,0,254,-1,-1,-1,MERGE_MAX);

   /* The sums carry their own prefix - 1_summ.wrk is Q1, 5_summ.wrk is Q24.
    * Their no-data byte is 14: bufr2wrk's precipitation table wraps past 254
    * back through 0, and the BUFR missing value lands on 14 - which reads as
    * 0.12 mm rather than as nothing. */
   for (i=0;i<5;i++) {
      sprintf(file,"%d_summ.wrk",i+1);
      sprintf(title,"Осадки %dч",sum_hours[i]);
      add_map(MAP_Q1+i,file,"precip_sum",title,FAM_SUM,sum_hours[i],255,14,-1,0,MERGE_IDW);
   }

   for (i=1;i<=10;i++) {
      sprintf(file,"dif_%d.wrk",i);
      sprintf(title,"ZDR %d",i);
      add_map(MAP_D1+i-1,file,"zdr",title,FAM_ZDR,i,121,-1,-1,0,MERGE_NEAR);
   }
   for (i=1;i<=10;i++) {
      sprintf(file,"vel_%d.wrk",i);
      sprintf(title,"Vr %d",i);
      add_map(MAP_V1+i-1,file,"velocity",title,FAM_VEL,i,255,-1,-1,0,MERGE_NEAR);
   }
}

/* index of a map in maps[], for the few places that name one directly */
int map_index(map_type id)
{
   int i;

   for (i=0;i<no_maps;i++) if (maps[i].mapid==id) return i;
   return 0;
}

/* Accessors for the Qt panel, which builds its product buttons from this
 * table rather than carrying a copy of it.  The labels stay behind: descr is
 * CP866 and the C++ side has no business decoding it, so it is handed the
 * family and the level and writes its own. */
int product_count(void)          { return no_maps; }
int product_family_of(int index)  { return index>=0 && index<no_maps ? (int)maps[index].family : -1; }
int product_level_of(int index)   { return index>=0 && index<no_maps ? maps[index].level : 0; }
int product_key_of(int index)     { return KEY_PRODUCT+index; }

/* did the file just read carry this map? */
int map_present(int index)
{
   return index>=0 && index<no_maps && maps[index].mapres!=0;
}

void read_cfg(char *wrk_path,char *image_cfg) {
FILE *pat;
char tik[4],pom[30],file[80],s1[80];
int a,b,c;

int i,j;
int size;

init_maps();

for (i=0;i<MaxPorts;i++) strcpy(in_dir[i],"");

if ((pat=fopen (wrk_path,"rt"))==NULL) {
  printf("\n\a7: File with paths not found: %s\n",wrk_path);
  exit(1); }
while (1) {
  if (fgets(s1,80,pat)==NULL) break;
  if (s1[0]==';') continue;
  i=0;  while((s1[i]!='\n') && (i<79)) i++;     s1[i]=0;
  for (i=0; i<3; i++) tik[i]=toupper(s1[i]);    tik[3]=0;
  if (!strcmp(tik,"GRF")) sscanf(s1,"%s %s",pom,grfdir);
  if (!strcmp(tik,"MAP")) sscanf(s1,"%s %s",pom,mapdir);
  if (!strcmp(tik,"CFG")) sscanf(s1,"%s %s",pom,cfgdir);
  for (j=0;j<MaxPorts;j++)  {
	  sprintf(pom,"IN%1d",j+1);
	  if (!strcmp(tik,pom)) sscanf(s1,"%s %s",pom,in_dir[j]);
  }

} /* while 1 */
fclose(pat);

sprintf(file,"%s/%s",cfgdir,image_cfg);
  if ((pat=fopen (file,"rt"))==NULL) {
  printf("File with config not found: %s\n",file);
  exit(1); }

while (1) {
  if (fgets(s1,80,pat)==NULL) break;
  if (s1[0]==';') continue;
  sscanf(s1,"%s",pom);
  if (!strcmp(pom,"WINDOWX") || !strcmp(pom,"WindowX")) {
	  sscanf(s1,"%s %d",pom,&size);
	  WINDOW_XSIZE=size;
  }
  else if (!strcmp(pom,"WINDOWY") || !strcmp(pom,"WindowY")) {
	  sscanf(s1,"%s %d",pom,&size);
	  WINDOW_YSIZE=size;
  }
  else if (!strcmp(pom,"CENTERX") || !strcmp(pom,"CenterX")) {
     sscanf(s1,"%s %d,%d,%d",pom,DX,DX+1,DX+2);
     LU=(DX[0]+(float)DX[1]/60+(float)DX[2]/60/60)*RAD;
  }
  else if (!strcmp(pom,"CENTERY") || !strcmp(pom,"CenterY")) {
	  sscanf(s1,"%s %d,%d,%d",pom,SY,SY+1,SY+2);
     BU=(SY[0]+(float)SY[1]/60+(float)SY[2]/60/60)*RAD;
  }
  else if (!strcmp(pom,"CURSORX") || !strcmp(pom,"CursorX")) {
	  sscanf(s1,"%s %d,%d,%d",pom,&a,&b,&c);
	  LC=(a+(float)b/60+(float)c/60/60)*RAD;
  }
  else if (!strcmp(pom,"CURSORY") || !strcmp(pom,"CursorY")) {
     sscanf(s1,"%s %d,%d,%d",pom,&a,&b,&c);
     BC=(a+(float)b/60+(float)c/60/60)*RAD;
  }
  else if (!strcmp(pom,"MAPSIZE") || !strcmp(pom,"MapSize")) {
      sscanf(s1,"%s %d",pom,&size);
      MSIZE_int=size;
      MSIZE=size*2;
  }
  else if (!strcmp(pom,"MPIX") ) {
      sscanf(s1,"%s %f",pom,&MPIX);
  }
  else if (!strcmp(pom,"SECOND") || !strcmp(pom,"Second")) {
		sscanf(s1,"%s %d",pom,&size);
		Second=size;
  }
  else if (!strcmp(pom,"CENTERNAME") || !strcmp(pom,"CenterNam"))
      sscanf(s1,"%s %s",pom,center_name);
  else if (!strcmp(pom,"CENTERH") || !strcmp(pom,"CenterH"))
		sscanf(s1,"%s %d",pom,&height);
} /* while 1 */
fclose(pat);

/* The legend used to be built here, by turning config/porog.* into
 * config/thresh.* - fifteen levels squeezed onto the fifteen colours
 * config/palette could spare.  Products carry a palette of their own now
 * (the .pal files under config/palettes, see palette.c), so none of that
 * is read any more.
 */
}

void init_files(void) {
int i;
 for (i=0; i<no_maps; i++) {
    maps[i].bufhead=malloc(8);
    maps[i].bufdata=NULL;   /* allocated when the product first turns up */
 }
 mapbuffer=_MALLOC((long)MSIZE*MSIZE);
 if (mapbuffer==NULL) {
    printf("Can't allocate map buffer!\n");
    exit(3);
 }


}

void de_init_files(void) {
int i;
 free(mapbuffer);
 for (i=0; i<no_maps; i++) {
   free(maps[i].bufhead);
   free(maps[i].bufdata);
 }
}

/* One mosaic buffer is MSIZE_int squared - a couple of megabytes at the usual
 * map size - and there are dozens of products now, most of which any given
 * radar does not send.  Allocate one the first time its product arrives. */
static unsigned char *map_buffer(int i) {
  if (maps[i].bufdata==NULL) {
     maps[i].bufdata=malloc((long)MSIZE_int*MSIZE_int);
     if (maps[i].bufdata==NULL) {
        printf("Can't allocate the buffer for %s!\n",maps[i].filename);
        exit(3);
     }
     memset(maps[i].bufdata,maps[i].nodata,(long)MSIZE_int*MSIZE_int);
  }
  return maps[i].bufdata;
}

/* Undo the packing some radars apply to the pixel grid: every byte inverted
 * (num 1), or inverted with each run of `cod` bytes reversed (num 2).  It is a
 * property of the file, so it runs once per file even when two products read
 * the same one - doing it twice would undo it. */
static void unpack_grid(unsigned char *grid,int mapsize) {
int j,k,raz;
unsigned char *memr;

  switch (num) {
  case 1:
     for (j=0; j<mapsize*mapsize; j++) grid[j]=~grid[j];
     break;
  case 2:
     if (cod<=0) break;
     if ((memr=malloc(cod))==NULL) break;
     raz=mapsize*mapsize/cod;
     for (k=0; k<raz; k++) {
        for (j=0; j<cod; j++) memr[cod-1-j]=grid[k*cod+j];
        for (j=0; j<cod; j++) grid[k*cod+j]=~memr[j];
     }
     free(memr);
     break;
  default:
     break;
  }
}

void read_files(int number,int flag) { /* flag=1 if load current map only */

int i,j,port,slot,nslots;
char archive_name[100];
char filename[MaxNoMaps][20];
UzpFilesBuffer buffer[MaxNoMaps+1];
int map_slot[MaxNoMaps];  /* map -> the buffer holding its file, -1 if unread */
unsigned char *ptr;
int mapsize;

unzFile uf=NULL;
unz_file_info file_info;
int err;

flags=0;

  for (i=0; i<no_maps; i++) {
    if (maps[i].bufdata)
       memset(maps[i].bufdata,maps[i].nodata,(long)MSIZE_int*MSIZE_int);
    maps[i].mapres=0;
  }
  memset(header,0,sizeof(header));

  for (i=0; i<no_maps; i++)
    for (port=0;port<MaxPorts;port++) fk[i][port]=0;

  for (port=0;port<MaxPorts;port++) {
    SPEED[port]=-1;
    memset(ST[port],0,50);
    HMRL[port]=0;
    R[port]=0;
    masx[port]=0; masy[port]=0;
  }

  /* Work out what to unzip, once: the member name of every map wanted, with
   * one buffer per distinct name.  4_dbz_0.wrk backs two products - the rain
   * rate and the maximum reflectivity - and reading it twice would also
   * unpack it twice, which is what the old "quick hack for MAP_0" worked
   * around rather than fixed. */
  nslots=1;                                   /* buffer[0] is header.wrk */
  for (i=0; i<no_maps; i++) {
    map_slot[i]=-1;
    if (flag && i!=current_map) continue;
    if (isdigit((unsigned char)maps[i].filename[0]) && maps[i].filename[1]=='_')
       strcpy(filename[i],maps[i].filename);  /* the sums carry their own */
    else
       sprintf(filename[i],"%c_%s",current_prefix,maps[i].filename);
    for (j=0; j<i; j++)
       if (map_slot[j]>=0 && !strcmp(filename[j],filename[i])) {
          map_slot[i]=map_slot[j];
          break;
       }
    if (map_slot[i]<0) {
       map_slot[i]=nslots;
       buffer[nslots].fname=filename[i];
       nslots++;
    }
  }
  buffer[0].fname=head_wrk;
  buffer[nslots].fname=NULL;
  cur=(int)current_prefix;

	for (port=MaxPorts-1;port>=0;port--)
    if ((Files[number].flag & PORT_BIT(port)) /*&& (show_maps & (1<<port))*/ ) {
                                                /* if exist Map file */
     sprintf(archive_name,"%s/port%1d/%02d%02d%02d%02d.%02dm",
       mapdir,port+1,Files[number].FileYear,Files[number].FileMonth,
        Files[number].FileDay,Files[number].FileHour,Files[number].FileMinute);

     for (i=0;i<nslots;i++) { buffer[i].strlength=0; buffer[i].strptr=NULL; }

#define CASESENSITIVITY (2)

     if ((uf=unzOpen(archive_name))==NULL) {
        printf ("Cannot open zip file %s\n",archive_name);
        continue;
     }

     for (i=0;i<nslots;i++) {
       /* a product this radar does not send is the normal case now, not an
        * error worth a line per port per file */
       if (unzLocateFile(uf,buffer[i].fname,CASESENSITIVITY)!=UNZ_OK) continue;
       if (unzGetCurrentFileInfo(uf,&file_info,NULL,0,NULL,0,NULL,0)!=UNZ_OK) {
          printf("error getting info for file %s\n",buffer[i].fname);
          continue;
       }
       buffer[i].strlength=file_info.uncompressed_size;
       if (unzOpenCurrentFile(uf)!=UNZ_OK) {
          printf("error opening file %s, size=%ld\n",
                 buffer[i].fname,buffer[i].strlength);
          buffer[i].strlength=0;
          continue;
       }
       buffer[i].strptr=malloc(buffer[i].strlength);
       if (buffer[i].strptr!=NULL)
          err=unzReadCurrentFile(uf,buffer[i].strptr,buffer[i].strlength);
       if (buffer[i].strptr==NULL || err!=(int)buffer[i].strlength) {
          printf("error reading file %s into memory, size=%ld\n",
                 buffer[i].fname,buffer[i].strlength);
          free(buffer[i].strptr);
          buffer[i].strptr=NULL;
          buffer[i].strlength=0;
       }
       unzCloseCurrentFile(uf);
     }
     unzClose(uf);

     if (buffer[0].strlength!=sizeof(header)) {  /* no passport, no maps */
        for (i=0;i<nslots;i++) free(buffer[i].strptr);
        continue;
     }
     memcpy(header,buffer[0].strptr,buffer[0].strlength);
     fp=1;
     memset(ST[port],0,50);
     memcpy(ST[port],header+5,25);

    L0[port]=((float)header[47]+(float)header[48]/60+(float)header[49]/60/60)*RAD;
    B0[port]=((float)header[50]+(float)header[51]/60+(float)header[52]/60/60)*RAD;

    SPEED[port]=(header[112]==255)?-1:header[112];
    AZIMUT[port]=(((int)header[113])<<8) + (int)header[114];
    CORR[port]=(int)header[115];
    if (AZIMUT[port]==511) SPEED[port]=-1;    // missing
    if (SPEED[port]==150 && AZIMUT[port]==255)  SPEED[port]=-1;    // missing, workaround for aksopri BUFRS
    if (SPEED[port]==0 && AZIMUT[port]==0)  SPEED[port]=-1;    // missing

		R[port]=(int)header[120];
		bww=0;bw=0;
	 bww=(float)header[39]/100.;
	 bw=bww/4.;
	 tetu[port]=(float)header[57]/100.;
	 tetu[port]=tetu[port]-bw;
		if(tetu[port]<bw) tetu[port]=bw;
	 naa[port]=tetu[port]*RAD;
		num=(int)header[101];
		cod=(int)header[102];
      HMRL[port]=(float)header[53]/10.;

     /* the pixel grids: check the size against the passport and unpack, one
      * pass per file rather than one per product reading it */
     for (i=1;i<nslots;i++) {
        if (buffer[i].strlength<=8) continue;
        mapsize=(unsigned char)buffer[i].strptr[2];
        if (mapsize<100) mapsize*=10;
        if (buffer[i].strlength!=(unsigned long)mapsize*mapsize+8) {
           buffer[i].strlength=0;              /* not the grid we expect */
           continue;
        }
        unpack_grid((unsigned char *)buffer[i].strptr+8,mapsize);
     }

     for (i=0;i<no_maps;i++) {
        slot=map_slot[i];
        if (slot<0 || buffer[slot].strlength==0) continue;
        memcpy(maps[i].bufhead,buffer[slot].strptr,8);
        maps[i].mapres=(float)maps[i].bufhead[1]/10.;
        W[port][i]=(float)maps[i].bufhead[0]/10.;
        mapsize=maps[i].bufhead[2];
        if (mapsize<100) mapsize*=10;
        fk[i][port]=1;
        if (!(show_maps & PORT_BIT(port))) continue;

        /* Fold the older no-data bytes onto the current one, so the merge,
         * the interpolation and the palette all have a single marker to test
         * against.  Idempotent, which matters because two products share one
         * grid.  See the maps[] table for where the old values come from. */
        {
           unsigned char *grid=(unsigned char *)buffer[slot].strptr+8;
           int a;
           for (a=0;a<2;a++) {
              if (maps[i].nodata_alt[a]<0) continue;
              for (j=0;j<mapsize*mapsize;j++)
                 if (grid[j]==maps[i].nodata_alt[a]) grid[j]=maps[i].nodata;
           }
        }

        ptr=get_ptr(L0[port],B0[port],maps[i].mapres,mapsize,MSIZE_int);
        koord((unsigned char *)buffer[slot].strptr+8,ptr,mapsize,port);
        switch (maps[i].merge) {
        case MERGE_MAX:
           walk_table_max((unsigned char *)buffer[slot].strptr+8,map_buffer(i),
                          ptr,mapsize,MSIZE_int,maps[i].nodata);
           break;
        case MERGE_NEAR:
           walk_table_near((unsigned char *)buffer[slot].strptr+8,map_buffer(i),
                           ptr,mapsize,MSIZE_int,maps[i].mapres,port,i,
                           maps[i].nodata);
           break;
        default:
           walk_table_idw((unsigned char *)buffer[slot].strptr+8,map_buffer(i),
                          ptr,mapsize,MSIZE_int,maps[i].mapres,port,i,
                          maps[i].nodata);
           break;
        }
     }

     for (i=0;i<nslots;i++) free(buffer[i].strptr);
	}

	for (i=0; i<no_maps; i++)
	   if (maps[i].mapres!=0 && maps[i].bufdata)
	      interpolation(maps[i].bufdata,MSIZE_int,maps[i].nodata);

        set_cur_map(maps[current_map].mapid);

}

void set_cur_map(map_type id) {
int i;
 for (i=0; i<no_maps; i++) if (maps[i].mapid==id && maps[i].mapres!=0) {
	 current_map=i;
	 break;
 }
 /* With this many optional products, asking for one the file does not carry
  * is routine.  Fall back to any product that is there rather than expand a
  * buffer that was never allocated. */
 if (maps[current_map].mapres==0 || maps[current_map].bufdata==NULL)
   for (i=0; i<no_maps; i++)
      if (maps[i].mapres!=0 && maps[i].bufdata!=NULL) { current_map=i; break; }

 if (maps[current_map].bufdata==NULL) {
   MRES=0;                       /* draw_map() keeps the previous frame */
   printf ("no product in this file\n");
   return;
 }

 if (maps[current_map].family==FAM_HEIGHT)
   expand_heights(maps[current_map].bufdata,mapbuffer,MSIZE_int);
 else expand (maps[current_map].bufdata,mapbuffer,MSIZE_int);
 MRES=maps[current_map].mapres/2;
 if (MRES==0)
   printf ("map resolution=0!\n");


}

void set_cur_prefix(int cur_file,char prefix) {
int old_map=maps[current_map].mapid;
char old_prefix=

current_prefix;
int i;
int flag=0;

 fp=1;
 current_prefix=prefix;
 read_files(cur_file,0);

 for (i=0; i<no_maps; i++)
    if (maps[i].mapres!=0) {
       current_map=i;
       flag=1;
      break;
 }

// if (flag==0) {
//   current_prefix=old_prefix;
//   read_files(cur_file,0);
// }

 set_cur_map(old_map);
}


#define SIZE 3
int get_map_data(map_type id,int xcoord,int ycoord) {
int i,j,x,y;
unsigned char in_buffer[SIZE][SIZE],out_buffer[SIZE*2][SIZE*2];

 for (i=0; i<no_maps; i++) if (maps[i].mapid==id) break;
 if (i==no_maps || maps[i].mapres==0 || maps[i].bufdata==NULL)
   return -1;                                   /* no map exist */
 x=xcoord/2;y=ycoord/2;
 for (j=0;j<SIZE; j++)
     memcpy(in_buffer[j],maps[i].bufdata+(long)(y+j)*MSIZE_int+x,SIZE);
 if (id==MAP_H)
   expand_heights((unsigned char *)in_buffer,(unsigned char _HUGE *)out_buffer,SIZE);
 else expand ((unsigned char *)in_buffer,(unsigned char _HUGE *)out_buffer,SIZE);
 return out_buffer[ycoord-y*2][xcoord-x*2];
}

/* The reading at a point, or -1 when there is none.  Products no longer agree
 * on which byte means "no data" - see the maps[] table - so the readout column
 * cannot just compare against 254 any more. */
int map_value(map_type id,int xcoord,int ycoord) {
int i,value;

 value=get_map_data(id,xcoord,ycoord);
 if (value<0) return -1;
 i=map_index(id);
 if (value==maps[i].nodata) return -1;
 return value;
}

