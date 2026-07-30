#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "image.h"


#ifdef __linux__
  #define _ptr _IO_read_ptr
#endif

#define PIXEL    0      /* признак точки*/
#define LINE     1      /*  -"-    линии*/
#define CIRCLE   2      /*  -"-    окружности*/
#define CELL     5      /*  -"-    цель   */
#define CELP     6
#define TEXT     7
#define POLY     8
#define COLOR    9      /* change color */
  #define CELU     10
#define MODE     11



#define MAX_POLY 1500    /* max polynom lines */

void linear(int *x,int *y,float xpd,int mapres) {

   *x=(*x)*xpd*10/mapres-x_left*xpd+WINDOW_XSIZE/2;
   *y=(-*y)*xpd*10/mapres-y_up*xpd+WINDOW_YSIZE/2;
}

void decart(int *x,int *y) {
float f,l,X,Y;

    l=((*x)/100+(float)((*x)%100)/60)*RAD;
    f=((*y)/100+(float)((*y)%100)/60)*RAD;
    conv_back(LU,BU,l,f,&X,&Y);
    *x=X>0 ? X+.5 : X-.5;
    *y=Y>0 ? Y+.5 : Y-.5;
}

void clear_tlo(struct info inf_ptr[],int *max_number) {
int i;
  for (i=0;i<*max_number;i++)
    if (inf_ptr[i].type==TEXT || inf_ptr[i].type==POLY)
	  free(inf_ptr[i].ptr);
  *max_number=0;

}

void read_tlo (char *filename,struct info inf_ptr[],int *max_number) {

char     s1[80];
int      *p;
int	 type,i;
FILE     *in;
int      x1,y1,x2,y2;
int      r,c;
char     g;
int number=0;
FILE *file;
int line=0;

    clear_tlo(inf_ptr,max_number);
       in=fopen(filename,"rt");
       while(!feof(in) && fscanf(in,"%3s",s1)==1 && number<MaxTLO)  {
		line++;
		 if (!strcmp(s1,"CEL") || !strcmp(s1,"cel")) type=CELL;
	    else if (!strcmp(s1,"CLP") || !strcmp(s1,"clp")) type=CELP;
             else if (!strcmp(s1,"CEU") || !strcmp(s1,"ceu")) type=CELU;
            else if (!strcmp(s1,"MOD") || !strcmp(s1,"mod")) type=MODE;
            else if (!strcmp(s1,"PIX") || !strcmp(s1,"pix")) type=PIXEL;
	    else if (!strcmp(s1,"LIN") || !strcmp(s1,"lin")) type=LINE;
	    else if (!strcmp(s1,"CIR") || !strcmp(s1,"cir")) type=CIRCLE;
	    else if (!strcmp(s1,"TXT") || !strcmp(s1,"txt")) type=TEXT;
	    else if (!strcmp(s1,"PLY") || !strcmp(s1,"ply")) type=POLY;
	    else if (!strcmp(s1,"COL") || !strcmp(s1,"col")) type=COLOR;
	    else if (!strcmp(s1,"REM") || !strcmp(s1,"rem") || s1[0]==';') {
	       { int ch; while ((ch=getc(in))!=EOF && ch!='\n') ; }
	       continue;
	    } else { printf("Invalid command code: %s in line %d\n",s1,line); exit(1); }
	    switch (type) {
		   case COLOR:
			fscanf(in,"%d",&c);
			inf_ptr[number].type=COLOR;
			inf_ptr[number].x1=c;
			break;
                     case MODE:
			fscanf(in,"%d",&c);
			inf_ptr[number].type=MODE;
			inf_ptr[number].x1=c;
			break;
                    case CELL: break;
		   case CELP: break;
                   case CELU:
		       fscanf(in," %c, %d %d ",&g,&x1,&y1);
			if(g=='g') decart(&x1,&y1);
			inf_ptr[number].type=CELU;
			inf_ptr[number].x1=x1;
			inf_ptr[number].y1=y1;
			break;

		   case PIXEL:
		       fscanf(in," %c, %d %d, %d",&g,&x1,&y1,&c);
			if(g=='g') decart(&x1,&y1);
			inf_ptr[number].type=PIXEL;
			inf_ptr[number].x1=x1;
			inf_ptr[number].y1=y1;
			break;
		   case LINE:
			fscanf(in," %c, %d %d, %d %d",&g,&x1,&y1,&x2,&y2);
			if (g=='g') {
			  decart(&x1,&y1);
			  decart(&x2,&y2);
			}
			inf_ptr[number].type=LINE;
			inf_ptr[number].x1=x1;
			inf_ptr[number].y1=y1;
			inf_ptr[number].x2=x2;
			inf_ptr[number].y2=y2;
			break;
		   case CIRCLE:
			fscanf(in," %c, %d %d, %d",&g,&x1,&y1,&r);
			if(g=='g') decart(&x1,&y1);
			inf_ptr[number].type=CIRCLE;
			inf_ptr[number].x1=x1;
			inf_ptr[number].y1=y1;
			inf_ptr[number].x2=r;
			break;
		   case TEXT:
			fscanf(in," %c, %d %d, %79s",&g,&x1,&y1,s1);
			if(g=='g') decart(&x1,&y1);
			inf_ptr[number].type=TEXT;
			inf_ptr[number].x1=x1;
			inf_ptr[number].y1=y1;
			inf_ptr[number].size=strlen(s1)+1;
			inf_ptr[number].ptr=malloc(strlen(s1)+1);
			strcpy((char *)inf_ptr[number].ptr,s1);
			break;
		   case POLY:
			fscanf(in," %c, %d,",&g,&c);
			inf_ptr[number].type=POLY;
			inf_ptr[number].ptr=p=(int *)malloc(c*2*sizeof(int));
			inf_ptr[number].size=c;
			for(i=0;i<c;i++)
			{
			  fscanf(in,"%d %d,",&x1,&y1);
			  if(g=='g') decart(&x1,&y1);
			  *p++=x1;
			  *p++=y1;
			}  /* for */
			break;
	    } /* switch */
	    number++;
       } /* while */
       fclose(in);
       *max_number=number;

}

void draw_tlo(struct info inf_ptr[],int max_number) {
int number;
int mode;
int x1,y1,x2,y2,r;
int i;
int *p,*p1;
char temp[80];
int mapres=MRES*10;
int poly_buf[MAX_POLY][2];
//TPoint pt[MAX_POLY];

   current_color=15;  /* white */
   for(number=0;number<max_number; number++)
	 switch (inf_ptr[number].type) {
		case COLOR: current_color=inf_ptr[number].x1;
			    break;
                 case MODE: mode=inf_ptr[number].x1;
			    break;


		case PIXEL: x1=inf_ptr[number].x1;
			    y1=inf_ptr[number].y1;
			    linear(&x1,&y1,MPIX,mapres);
			    my_pixel(x1,y1);
			    break;
                  case CELU:    for(i=0;i<12;i++)
                { x1=inf_ptr[number].x1;
		 y1=inf_ptr[number].y1;
                             x2=x1+200*sin(i*30*RAD);
			   y2=y1+200*cos(i*30*RAD);
			    linear(&x1,&y1,MPIX,mapres);
			    linear(&x2,&y2,MPIX,mapres);
			    my_line(x1,y1,x2,y2);
                            }
                           break;


		case LINE:  x1=inf_ptr[number].x1;
			    y1=inf_ptr[number].y1;
			    x2=inf_ptr[number].x2;
			    y2=inf_ptr[number].y2;
			    linear(&x1,&y1,MPIX,mapres);
			    linear(&x2,&y2,MPIX,mapres);
			    my_line(x1,y1,x2,y2);
			    break;
		case CIRCLE:x1=inf_ptr[number].x1;
			    y1=inf_ptr[number].y1;
			    r=inf_ptr[number].x2*MPIX*10/mapres;
			    linear(&x1,&y1,MPIX,mapres);
			    my_circle(x1,y1,r);
			    break;
		case TEXT:
             x1=inf_ptr[number].x1;
			    y1=inf_ptr[number].y1;
			    linear(&x1,&y1,MPIX,mapres);
			    my_textxy(x1,y1,(char *)inf_ptr[number].ptr);
			    break;
		case POLY:
			    p1=(int *)inf_ptr[number].ptr;
             if (inf_ptr[number].size>MAX_POLY) break;
			    for(i=0;i<(int)inf_ptr[number].size;i++)
			    {
			      x1=*p1++;
			      y1=*p1++;
			      linear(&x1,&y1,MPIX,mapres);
			      poly_buf[i][0]=x1;
			      poly_buf[i][1]=y1;
			    }  /* for */
			    my_poly(inf_ptr[number].size,(int *)poly_buf);
			    break;
   }
}

void set_cur_geogr(int number) {
char temp[80];
 sprintf(temp,"%s/graf.k%d",grfdir,number);
 read_tlo(temp,grafs,&max_grafs);
}
