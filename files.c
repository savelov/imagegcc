
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
int sto[15];
int cu=0;

char current_prefix='4';
int current_map=4;
float MRES;  /* current map resolution */
unsigned char _HUGE *mapbuffer;

unsigned char stormmsg[PMSG][c10];
unsigned char header[128];

float LU,BU,LC,BC;
int DX[3],SY[3];
int height;           /* высот  центр  сбор  в метр х */
unsigned int MSIZE;	      /* р змер к рты */
unsigned int MSIZE_int;
float SPEED[MaxPorts];
float W[MaxPorts][20];
signed int AZIMUT[MaxPorts];
float           bw,bww,tetu[MaxPorts],naa[MaxPorts];
int CORR[MaxPorts];
int R[MaxPorts];
int num;
int cod;

int Sezon;
/* Z-R coefficients, used only here.  They must stay file local: coord.c
 * has its own float B[MaxPortTables] and the two used to share storage. */
static int A,B;
int SezonP[MaxPorts], AP[MaxPorts], BP[MaxPorts];
// int Sezon[MaxPorts];
float As,Bs,Aw,Bw,aa,bb;
float BW;
float HMRL[MaxPorts];
float L0[MaxPorts],B0[MaxPorts];
char center_name[15];
int fk[25][MaxPorts];


#define MaxNoMaps 20
FILE *pat;
int flags=0;
int masx[MaxPorts], masy[MaxPorts];
struct map_info maps[] = {
 {MAP_H,"heigh.wrk","h","Высот  км ",0,NULL,NULL},
  {MAP_S,"storm.wrk","s","Оп сн.явл.",0,NULL,NULL},
 {MAP_P,"dbz_0.wrk","p","Инт. мм/ч",0,NULL,NULL},
 {MAP_0,"dbz_0.wrk","z","Отр ж-сть",0,NULL,NULL},
#ifndef __TURBOC__
 {MAP_1,"dbz_1.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_2,"dbz_2.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_3,"dbz_3.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_4,"dbz_4.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_5,"dbz_5.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_6,"dbz_6.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_7,"dbz_7.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_8,"dbz_8.wrk","z","Отр ж-сть",0,NULL,NULL},
 {MAP_Q,"summ.wrk" ,"q","Ос дки мм.  ",0,NULL,NULL}
#endif
};

int no_maps=sizeof(maps)/sizeof(maps[0]);

void read_cfg(char *wrk_path,char *image_cfg) {
FILE *pat,*out;
char tik[4],pom[30],file[80],s1[80],s3[80];
char na[80],nam[80];
char str[16][80];
float q;

int zd,ip;
int a,b,c;

int i,j;
int size;

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

/* zero, not spaces: the rows are printed with %s and the ones this loop
   does not fill would have no terminator at all */
memset(str,0,sizeof str);
sprintf(nam,"%s/porog.q",grfdir);
sprintf(na,"%s/thresh.q",grfdir);
if ((pat=fopen (nam,"rt"))==NULL) {
  printf ("\n\a5: Error in file - %s\n",nam);  getch();  exit(1); }

 fgets(s1,79,pat);
 sprintf(str[0],"%s",s1);
/* fgets(s1,79,pat);
 sprintf(str[1],"    :%s",s1);  */
 fgets(s3,79,pat);

for (i=0; i<=12; i++)
 {
	memset(s1,0,80);
  if (fgets (s1,79,pat)==NULL)  {
	printf ("\n\a5: Error in file - %s\n",nam);
	delay (500);  break;  }
  sscanf(s1,"%f",&q);
/*	zd=64.*log10(floor(q*10.+0.5)+1.);*/
		zd=64.*log10(q*10.+1.);
	sprintf(str[i+1],"%i    :%s",zd,s3);
	memset(s3,0,80);
	if(i==0) {memset(s1,0,80); sprintf(s1,"следы ос\n");}
  strcpy(s3,s1);
  }
	 sprintf(str[i+1],"254      :>%i",(int)q);
 out=fopen (na,"wt");
 for(i=0;i<=15;i++)
 fprintf(out,"%s",str[i]);
   fclose(out);
   fclose (pat);

  memset(str,0,sizeof str);
  sprintf(nam,"%s/porog.z",grfdir);
  sprintf(na,"%s/thresh.z",grfdir);
  if ((pat=fopen (nam,"rt"))==NULL) {
     printf ("\n\a5: Error in file - %s\n",nam);
     getch();  exit(1);               }
 /* out=fopen (na,"wt");*/

 fgets(s1,79,pat);
 sprintf(str[0],"%s",s1);
 fgets(s3,79,pat);

 for (i=0; i<=12; i++)
 {
  memset(s1,0,80);

  if (fgets (s1,79,pat)==NULL)  {
  printf ("\n\a5: Error in file - %s\n",nam);
	delay (500);  break;    }
  sscanf(s1,"%i",&ip);

  zd=(floor)((30*ip-15)/10+1);
  sprintf(str[i+1],"%i    :%s",zd,s3);
  memset(s3,0,80);
  strcpy(s3,s1);
 }
   sprintf(str[i+1],"254      :>%i",ip);
  out=fopen (na,"wt");
  for(i=0;i<=15;i++)
  fprintf(out,"%s",str[i]);
   fclose(out);
   fclose(pat);

  memset(str,0,sizeof str);
  sprintf(nam,"%s/porog.h",grfdir);
  sprintf(na,"%s/thresh.h",grfdir);
  if ((pat=fopen (nam,"rt"))==NULL) {
       printf ("\n\a5: Error in file - %s\n",nam);
       getch();  exit(1);               }
/*  out=fopen (na,"wt");*/

 fgets(s1,79,pat);
 sprintf(str[0],"%s",s1);
 fgets(s3,79,pat);
 for (i=0; i<=12; i++)
 {
  memset(s1,0,80);
  if (fgets (s1,79,pat)==NULL)  {
	printf ("\n\a5: Error in file - %s\n",nam);
	delay (500);  break;    }
  sscanf(s1,"%f",&q);
  zd=q*10.-1.;
  sprintf(str[i+1],"%i    :%s",zd,s3);
  memset(s3,0,80);
  strcpy(s3,s1);
 }
   sprintf(str[i+1],"254      :>%i",(int)q);
   out=fopen (na,"wt");
   for(i=0; i<=14; i++)
   fprintf(out,"%s",str[i]);
   fclose(out);
   fclose(pat);

 sprintf(s1,"%s/thresh.s",grfdir);
 if ((pat=fopen (s1,"rt"))==NULL) {
     printf ("\n\a5: Error in file - %s\n",s1);
     getch();  exit(1); }
  memset (stormmsg,' ',PMSG*c10);
 if (fgets (stormmsg[15],c10-1,pat)==NULL) {
    printf ("\n\a5: Error in file - %s\n",s1);
    getch();  exit(1); }
 ip=0; while( (stormmsg[15][ip]!='\n')&&(ip<c10) ) ip++;  stormmsg[15][ip]=0;
 for (i=0; i<14; i++)
 {
  memset(s1,0,80);
  memset(s3,0,80);
  if (fgets (s1,79,pat)==NULL)  {
    printf ("\n\a5: Error in file - %s\n",s1);  delay (500);  break;  }
  ip=0; while( (s1[ip]!='\n')&&(s1[ip]!=' ')&&(s1[ip]!=0x00) ) ip++;
  strncpy(s3,s1,ip);
  zd=atoi(s3);
  sto[i]=zd;
  while ((s1[ip]==' ')&&(s1[ip]!='\n')&&(s1[ip]!=0x00)&&(ip<80)) ip++;
  zd=++ip; ip=0;
  while ((s1[ip+zd]!='\n')&&(s1[ip+zd]!=0x00)&&(ip<c10)&&(ip+zd<80)) {
  stormmsg[i][ip]=s1[ip+zd];  ip++; }
  stormmsg[i][ip]=0;

} /* end for i */
fclose (pat);

for (zd=0; zd<PMSG; zd++) stormmsg[zd][c10-1]=0x00;
}

void init_files(void) {
int i;
 for (i=0; i<no_maps; i++) {
    maps[i].bufhead=malloc(8);
    maps[i].bufdata=malloc(MSIZE_int*MSIZE_int);
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

void read_files(int number,int flag) { /* flag=1 if load current map only */

int i,port,zd,j;
FILE *map,*out;
char archive_name[100],s[80],s1[80],pom[80];
char nam[80],na[80],s3[80];
 char filename[MaxNoMaps][20];
 UzpFilesBuffer buffer[MaxNoMaps];

int error,tt;
 unsigned char *ptr;
unsigned char *tb;
unsigned char *memr;
int i1,k,raz;
float ww;
float zz,zzz;
char str[16][80];
int mapsize;
flags=0;


unzFile uf=NULL;
unz_file_info file_info;
int err;


  for (i=0; i<no_maps; i++) {
    memset(maps[i].bufdata,254,MSIZE_int*MSIZE_int);
    maps[i].mapres=0;
  }
  memset(header,0,sizeof(header));

  for(i=0; i<25; i++) {
  for (port=0;port<MaxPorts;port++)  fk[i][port]=0;}


  for (port=0;port<MaxPorts;port++) {SPEED[port]=-1;
				     memset(ST[port],0,50);
				     HMRL[port]=0;
				     R[port]=0; 
    HMRL[port]=0; R[port]=0;
    masx[port]=0;masy[port]=0;
  }
	for (port=MaxPorts-1;port>=0;port--)
//		for (port=0;port<=MaxPorts;port++)
    if ((Files[number].flag & (1LL<<port)) /*&& (show_maps & (1<<port))*/ ) {
                                                /* if exist Map file */
     sprintf(archive_name,"%s/port%1d/%02d%02d%02d%02d.%02dm",
       mapdir,port+1,Files[number].FileYear,Files[number].FileMonth,
        Files[number].FileDay,Files[number].FileHour,Files[number].FileMinute);

 		buffer[0].fname=head_wrk;
 		buffer[0].strlength=0;
      cur=(int)current_prefix;

   a: if (!flag) for (i=0;i<no_maps;i++) {
    sprintf(filename[i],"%c_%s",current_prefix,maps[i].filename);
		buffer[i+1].fname=filename[i];
		buffer[i+1].strlength=0;
  } else {
    sprintf(filename[0],"%c_%s",current_prefix,maps[current_map].filename);
		buffer[1].fname=filename[0];
		buffer[1].strlength=0;
    i=1;
  }
  buffer[i+1].fname=NULL;

//    UzpUnzipFiles(archive_name,buffer);

#define CASESENSITIVITY (2)

uf=unzOpen(archive_name);
    i=0;

if (uf==NULL) {
   printf ("Cannot open zip file %s\n",archive_name);
   return;
} else 

    while (buffer[i].fname!=NULL) {
       if (unzLocateFile(uf,buffer[i].fname,CASESENSITIVITY)!=UNZ_OK)
       {
          printf("file %s not found in the zipfile %s\n",buffer[i].fname,archive_name);
       } else {

// extract
       err = unzGetCurrentFileInfo (uf,&file_info,NULL,0,NULL,0,NULL,0);
       buffer[i].strlength=file_info.uncompressed_size;
       if (err!=UNZ_OK) printf("error getting info for  file %s, size=%ld\n", buffer[i].fname,buffer[i].strlength);
       err=unzOpenCurrentFile(uf);
       if (err!=UNZ_OK) {
             printf("error opening file %s, size=%ld\n", buffer[i].fname,buffer[i].strlength);
             buffer[i].strptr=NULL;
             buffer[i].strlength=0;
       } else {
             buffer[i].strptr= malloc(buffer[i].strlength);
             if (buffer[i].strptr!=NULL) err=unzReadCurrentFile(uf,buffer[i].strptr,buffer[i].strlength);
             if (err!=buffer[i].strlength || buffer[i].strptr==NULL)   {
                   printf("error reading file %s, into memory size=%ld\n", buffer[i].fname,buffer[i].strlength);
		   buffer[i].strlength=0;
		   free(buffer[i].strptr);
		    buffer[i].strptr=NULL;
             }
      }

       unzCloseCurrentFile(uf);
       }
	i++;
}

unzClose(uf);


    if (buffer[0].strlength!=sizeof(header)) return;
//if(buffer[i+1].strlength!=0)
//flags=1;/* else flags=0;*/

/*  Quick hack for MAP_0 */

 if (i>=4 && buffer[4].strlength==0) {
    buffer[4].strlength=buffer[3].strlength;
    buffer[4].strptr=malloc(buffer[4].strlength);
    memcpy(buffer[4].strptr,buffer[3].strptr,buffer[4].strlength);
 }
   memcpy(header,buffer[0].strptr,buffer[0].strlength);
//   if ((buffer[1].strlength==0)&&(fp==1))
//     {show_header(); continue;}

//      if ((buffer[1].strlength==0)&&(fp==0))
//      {cur=cur-1; current_prefix=cur; goto a;}
      fp=1;
    free(buffer[0].strptr);
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

//    printf ("port=%d, speed=%f, az=%d\n",port,SPEED[port],AZIMUT[port]);

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
 SezonP[port]=(int)header[104];
 Sezon=SezonP[0]; A=AP[0]; B=BP[0];
	  if((A==0)||(B==0)) {
sprintf(s1,"%s/config.mrl",cfgdir);
if((pat=fopen(s1,"rt"))==NULL) {
   printf("%s  file not found\n\a",s1); exit(1);}
for(i=0; i<16; i++)
fgets(pom,80,pat);  fscanf(pat,"%f,%f",&As,&Bs);
 fgets(pom,80,pat); fscanf(pat,"%f,%f",&Aw,&Bw);
fclose(pat);
if(Sezon==0) {aa=As; bb=Bs;} else {aa=Aw;bb=Bw;}
			       }
else {aa=A*2.; bb=B/10.;}


if(Sezon==0)
sprintf(nam,"%s/porog.pl",grfdir);
else
sprintf(nam,"%s/porog.pz",grfdir);

sprintf(na,"%s/thresh.p",grfdir);

pat=fopen(nam,"rt");
// out=fopen (na,"wt");
memset(str,0,sizeof str);
 fgets(s1,79,pat);
 sprintf(str[0],"%s",s1);
 fgets(s3,79,pat);

 for (j=0; j<=12; j++) {
  memset(s1,0,80);
 fgets(s1,79,pat);  sscanf(s1,"%f",&ww);
 zz=log10(aa); zzz=log10(ww);
 tt=30*zz+30*bb*zzz;
 sprintf(str[j+1],"%i    :%s",tt,s3);
  memset(s3,0,80);
  strcpy(s3,s1);
  }
    sprintf(str[j+1],"254    :>%i",(int)ww);
   out=fopen (na,"wt");
   for(j=0; j<=15; j++)
   fprintf(out,"%s",str[j]);
   fclose(out);
   fclose (pat);

		if (!flag) for (i=0; i<no_maps; i++) {
       if (buffer[i+1].strlength==0) continue; /* file not found or error */
       fk[i][port]=1;
       memcpy(maps[i].bufhead,buffer[i+1].strptr,8);
       maps[i].mapres=(float)maps[i].bufhead[1]/10.;
       W[port][i]=(float)maps[i].bufhead[0]/10.;
       mapsize=maps[i].bufhead[2];
       if (mapsize<100) mapsize*=10;

			 tb=malloc(mapsize*mapsize);
			 memr=malloc(mapsize*mapsize);
			 memcpy(tb,buffer[i+1].strptr+8,10000);
	 switch(num)
	  {
	  case 0: break;
	  case 1:
		 for(j=0; j<mapsize*mapsize; j++)
		 {  i1=~(unsigned char)tb[j];
		tb[j]=i1; }
		 break;
		case 2:
	//	if(cod!=125) break;
		 raz=mapsize*mapsize/cod;
		 for(k=0; k<raz; k++)
		{  for(j=0; j<cod; j++) {memr[cod-1-j]=tb[k*cod+j];}
	 for(j=0; j<cod; j++) {tb[k*cod+j]=~memr[j];}
		 }  break;

		 }/*switch*/
				free(memr);
				memcpy(buffer[i+1].strptr+8,tb,10000);
				free(tb);
 			 if (buffer[i+1].strlength==mapsize*mapsize+8U) {
 					ptr=get_ptr(L0[port],B0[port],maps[i].mapres,mapsize,MSIZE_int);
				if(show_maps & (1LL<<port))
		{   koord(buffer[i+1].strptr+8,ptr,mapsize,port);
				if(maps[i].mapid==MAP_H ) 
				    walk_table(buffer[i+1].strptr+8,maps[i].bufdata,ptr,mapsize,MSIZE_int);
				else if (maps[i].mapid==MAP_S) 
				    walk_tables(buffer[i+1].strptr+8,maps[i].bufdata,ptr,mapsize,MSIZE_int,maps[i].mapres,port,i);
				else
				walk_table0(buffer[i+1].strptr+8,maps[i].bufdata,ptr,mapsize,MSIZE_int,maps[i].mapres,port,i);
		}

			}

       free(buffer[i+1].strptr);
		} else {
             if (buffer[1].strlength!=0) {
                    fk[current_map][port]=1;
					memcpy(maps[current_map].bufhead,buffer[1].strptr,8);
					maps[current_map].mapres=(float)maps[current_map].bufhead[1]/10.;
					mapsize=maps[current_map].bufhead[2];
					if (mapsize<100) mapsize*=10;
       			 tb=malloc(mapsize*mapsize);
			 memr=malloc(mapsize*mapsize);
			 memcpy(tb,buffer[1].strptr+8,mapsize*mapsize);
	 switch(num)
	  {
	  case 0: break;
	  case 1:
		 for(j=0; j<mapsize*mapsize; j++)
		 {  i1=~(unsigned char)tb[j];
		tb[j]=i1; }
		 break;
		case 2:
	//	if(cod!=125) break;
		 raz=mapsize*mapsize/cod;
		 for(k=0; k<raz; k++)
		{  for(j=0; j<cod; j++) {memr[cod-1-j]=tb[k*cod+j];}
	 for(j=0; j<cod; j++) {tb[k*cod+j]=~memr[j];}
		 }  break;

		 }/*switch*/
				free(memr);
				memcpy(buffer[1].strptr+8,tb,10000);
				free(tb);
				if (buffer[1].strlength==mapsize*mapsize+8U) {
					 ptr=get_ptr(L0[port],B0[port],maps[current_map].mapres,mapsize,MSIZE_int);
					if(show_maps & (1LL<<port))
			{
				 koord(buffer[1].strptr+8,ptr,mapsize,port);

				if(maps[i].mapid==MAP_H ) 
				    walk_table(buffer[1].strptr+8,maps[current_map].bufdata,ptr,mapsize,MSIZE_int);
				else if (maps[i].mapid==MAP_S) 
				    walk_tables(buffer[1].strptr+8,maps[current_map].bufdata,ptr,mapsize,MSIZE_int,maps[current_map].mapres,port,current_map);
				else
				walk_table0(buffer[1].strptr+8,maps[current_map].bufdata,ptr,mapsize,MSIZE_int,maps[current_map].mapres,port,current_map);

				}
					}
					free(buffer[1].strptr);
			 }
    }
	}
	if (!flag) for (i=0; i<no_maps; i++) interpolation(maps[i].bufdata,MSIZE_int);
	else interpolation(maps[current_map].bufdata,MSIZE_int);

        set_cur_map(maps[current_map].mapid);

}

void set_cur_map(map_type id) {
int i;
 for (i=0; i<no_maps; i++) if (maps[i].mapid==id && maps[i].mapres!=0) {
	 current_map=i;
	 break;
 }
 if (maps[current_map].mapid==MAP_H)
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
 if (i==no_maps || maps[i].mapres==0) return -1; /* no map exist */
 x=xcoord/2;y=ycoord/2;
 for (j=0;j<SIZE; j++)
     memcpy(in_buffer[j],maps[i].bufdata+(long)(y+j)*MSIZE_int+x,SIZE);
 if (id==MAP_H)
   expand_heights((unsigned char *)in_buffer,(unsigned char _HUGE *)out_buffer,SIZE);
 else expand ((unsigned char *)in_buffer,(unsigned char _HUGE *)out_buffer,SIZE);
 return out_buffer[ycoord-y*2][xcoord-x*2];
}

