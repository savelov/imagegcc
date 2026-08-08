
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include "image.h"
#include <grx20.h>

extern GrContext *RamContext;

int cur_file;
port_mask show_maps=PORT_ALL;

int Second=10;
int xck,yck,xc2,yc2;
int xco,yco,xco1,xco2,yco1,yco2;
int flagl=0,flagv=0,flago=0, flaga=0;
/*int xc1,yc1;*/
int ot;
int xc1=240;
int yc1=240;
#define DEBUG
extern long colors[NUM_COLORS];

/* Only the animation reads the keyboard from inside draw_map(); see the note
 * on the poll at the end of draw_map() for why it must not happen otherwise. */
int anim_running=0;
int anim_stopped=0;      /* the last animation ended on a space, not at the end */

int animate (int begin,int end) {
int i;
int key;

anim_running=1;
anim_stopped=0;

for (i=begin; i<end; i++) {
   read_files(i,1);
   key=draw_map(1);
   showtime();
  if (key==' ') { anim_stopped=1; break; }

  select_product(key);   /* the product keys work mid animation too */
 }

 anim_running=0;
 return i;

}

void timer(void) { /* 1 per second */
static int counter=0;
DIR *inbound;
struct dirent *entry;
char file1[150],file2[150];
char flag=0;
int i;

  showtime();
  if (counter++>Second) {
	 counter=0;
	 for (i=0;i<MaxPorts;i++) if (strlen(in_dir[i])) {
             if ((inbound=opendir(in_dir[i]))!=NULL) {
                   while ((entry=readdir(inbound))!=NULL)
                       if (entry->d_name[0]!='.') {
                           sprintf(file1,"%s/%s",in_dir[i],entry->d_name);
			 //  sprintf(file2,"%s/port%d/%s",mapdir,i+1,entry->d_name);
			  // if (move_file(file1,file2)==0)
				 flag=1;


#ifdef DEBUG
                           flag=1; break;
#endif
					 }
		       unlink(file1);
                   closedir(inbound);
             }
	 }

	 if ((flag)&&(flaga==0)) {
		read_dir();
		cur_file=FilesRead-1;
		read_files(cur_file,0);
		draw_map(1);
	 }

  }

}

void mouse_move(int x,int y) {

	  /* which cell was painted here - surface_to_map() and draw_map()
	   * share one origin, so the readout cannot drift off the picture */
	  surface_to_map(x,y,&xco,&yco);
	  showdata(xco,yco);
	  xck=x; yck=y;
}




int mouse_click_left(int x,int y) { /* returns 1 if move mouse to center */
int xcoord,ycoord;
int min_left=WINDOW_XSIZE/MPIX/2+1-MSIZE/2;
int max_right=MSIZE/2-WINDOW_XSIZE/MPIX/2-1;
int min_up=WINDOW_YSIZE/MPIX/2+1-MSIZE/2;
int max_down=MSIZE/2-WINDOW_YSIZE/MPIX/2-1;
int flag;

    if (x>WINDOW_LEFT && x<(WINDOW_LEFT+WINDOW_XSIZE) &&
	y>WINDOW_UP && y<(WINDOW_UP+WINDOW_YSIZE)) {
                                        /* how far the clicked cell is from
                                           the one at the centre */
                                        surface_to_map(x,y,&xcoord,&ycoord);
                                        xcoord-=MSIZE/2+x_left;
                                        ycoord-=MSIZE/2+y_up;
					flag=1;

					if (x_left+xcoord<min_left) { x_left=min_left; flag=0; }
					else if (x_left+xcoord>max_right) { xcoord=max_right; flag=0; }
					else x_left+=xcoord;

					if (y_up+ycoord<min_up) { y_up=min_up; flag=0; }
					else if (y_up+ycoord>max_down) { y_up=max_down; flag=0; }
					else y_up+=ycoord;

					draw_map(1);
					return flag;
    }
		return 0;
}

void update_coords(void) {
int min_left=WINDOW_XSIZE/MPIX/2+1-MSIZE/2;
int max_right=MSIZE/2-WINDOW_XSIZE/MPIX/2-1;
int min_up=WINDOW_YSIZE/MPIX/2+1-MSIZE/2;
int max_down=MSIZE/2-WINDOW_YSIZE/MPIX/2-1;

		if (x_left<min_left) x_left=min_left;
		else if (x_left>max_right) x_left=max_right;
		if (y_up<min_up) y_up=min_up;
		else if (y_up>max_down) y_up=max_down;

}

void mouse_dclick_left(int x,int y) {
int xcoord,ycoord;

    if (x>WINDOW_LEFT && x<(WINDOW_LEFT+WINDOW_XSIZE) &&
	y>WINDOW_UP && y<(WINDOW_UP+WINDOW_YSIZE)) {
					if (MPIX<20) {
						MPIX++;
						update_coords();
						draw_map(1);
					}
    }
}

int mouse_click_right(int x,int y) {
	xc1=x;yc1=y;
	  mouse_move(x,y);
	  GrLine(xc1,yc1,xck,yck,colors[15]);
	 /* return 0;*/
	return 0;
}

/* Products are chosen by key.  There are more than forty of them now - the
 * reflectivity, differential reflectivity and velocity levels alone account
 * for thirty five - so a letter picks the family and the level is stepped
 * within it, rather than one key per product.
 *
 *   0..9   reflectivity levels 0 to 9      r  next reflectivity level
 *   p      rain rate                       d  next ZDR level
 *   h      echo top height                 v  next velocity level
 *   s      phenomena                       q  next precipitation sum
 *   [ ]    previous / next level of whatever family is on screen
 *
 * KEY_PRODUCT+i names maps[i] outright, which is how the Qt panel reaches a
 * product that has no key of its own.
 */
static int step_family(product_family family,int step)
{
int list[MaxNoMaps],count=0;
int i,at=-1,n;

  for (i=0;i<no_maps;i++)
     if (maps[i].family==family) list[count++]=i;
  if (count==0) return 0;

  for (i=0;i<count;i++) if (list[i]==current_map) at=i;

  if (at<0) {          /* entering the family: first level that has data */
     for (i=0;i<count;i++) if (map_present(list[i])) break;
     if (i==count) i=0;
     set_cur_map(maps[list[i]].mapid);
     return 1;
  }

  for (n=1;n<=count;n++) {
     i=((at+n*step)%count+count)%count;
     if (map_present(list[i])) {
        set_cur_map(maps[list[i]].mapid);
        break;
     }
  }
  return 1;            /* the family was recognised even if nothing moved */
}

int select_product(int key) {

  if (key>=KEY_PRODUCT && key<KEY_PRODUCT+no_maps) {
     set_cur_map(maps[key-KEY_PRODUCT].mapid);
     return 1;
  }

  switch (key) {
     case '0': case '1': case '2': case '3': case '4':
     case '5': case '6': case '7': case '8': case '9':
                set_cur_map(MAP_0+key-'0');  return 1;
     case 'p':
     case 'P':  set_cur_map(MAP_P);          return 1;
     case 'h':
     case 'H':  set_cur_map(MAP_H);          return 1;
     case 's':
     case 'S':  set_cur_map(MAP_S);          return 1;
     case 'r':
     case 'R':  return step_family(FAM_DBZ,1);
     case 'q':
     case 'Q':  return step_family(FAM_SUM,1);
     case 'd':
     case 'D':  return step_family(FAM_ZDR,1);
     case 'v':
     case 'V':  return step_family(FAM_VEL,1);
     case ']':  return step_family(maps[current_map].family,1);
     case '[':  return step_family(maps[current_map].family,-1);
     default:   return 0;
  }
}

int key_pressed(int key) { /* Returns 0 if OK to continue, 1 if Quit */
int result;
int min_left=WINDOW_XSIZE/MPIX/2+1-MSIZE/2;
int max_right=MSIZE/2-WINDOW_XSIZE/MPIX/2-1;
int min_up=WINDOW_YSIZE/MPIX/2+1-MSIZE/2;
int max_down=MSIZE/2-WINDOW_YSIZE/MPIX/2-1;


// printf ("key=%d\n",key);

	/*	printf("\n =%i",key);*/
	if (select_product(key)) { draw_map(1); return 0; }

	switch (key)
		{

//		case 363:  return 1;
		case 27:   return 1;
		case 'a':
		case 'A':  result=archive();
			   if (result!=-1) {
                              if (anim_begin!=-1 && anim_end!=-1) animate(anim_begin,anim_end);
                              else cur_file=result;
                              read_files(cur_file,0);
                              break;
                           } else return 0;

		case 376:  set_cur_prefix(cur_file,'1');   break;
		case 377:  set_cur_prefix(cur_file,'2');   break;
		case 378:  set_cur_prefix(cur_file,'3');   break;
		case 379:  set_cur_prefix(cur_file,'4');   break;
		case 380:  set_cur_prefix(cur_file,'5');   break;
		case 381:  set_cur_prefix(cur_file,'6');   break;
		case 382:  set_cur_prefix(cur_file,'7');   break;

		case 315:  set_cur_geogr(1); break;
		case 316:  set_cur_geogr(2); break;
		case 317:  set_cur_geogr(3); break;
		case 318:  set_cur_geogr(4); break;
		case 319:  set_cur_geogr(5); break;
		case 320:  set_cur_geogr(6); break;
		case 321:  set_cur_geogr(7); break;
		case 322:  set_cur_geogr(8); break;

		case '+':  if (cur_file<FilesRead-1)
			{	cur_file++;
				read_files(cur_file,0);
		 } else
		 return 0; break;
		case '-':  if (cur_file>0) {
				cur_file--;
				read_files(cur_file,0);
		 } else return 0; break;
                case ' ': if (cur_file<FilesRead-1) {
                                 cur_file=animate(cur_file,FilesRead-1);
                                 read_files(cur_file,0);
                                 /* Stopped by the user: animate() has already
                                  * left this very frame on screen, so the
                                  * repaint below would only redraw what is
                                  * there.  Run to the end and it has not -
                                  * the last frame drawn was cur_file-1. */
                                 if (anim_stopped) return 0;
		 } else return 0; break;
		case '/':  cur_file=0;
		 read_files(cur_file,0);
		 break;
		case '*':
		case 'z':
		case 'Z': flaga=0; read_dir(); cur_file=FilesRead-1;
		 read_files(cur_file,0);
		 break;
		case 331:   /* Left */
			if (x_left>min_left+5) x_left-=5;
			else if (x_left==min_left) return 0;
			else x_left=min_left;
			break;
		case 333:   /* Right */
			if (x_left<max_right-5) x_left+=5;
			else if (x_left==max_right) return 0;
			else x_left=max_right;
			break;
		case 328:   /* Up */
			if (y_up>min_up+5) y_up-=5;
			else if (y_up==min_up) return 0;
			else y_up=min_up;
			break;
		case 336:   /* Down */
			if (y_up<max_down-5) y_up+=5;
			else if (y_up==max_down) return 0;
			else y_up=max_down;
			break;
		case 'y':
//                         ViewForecast(cur_file);
			  break;
		case 't':
//			 ViewStorm(cur_file);

			 break;
		case '.':
		case '>':   if (MPIX<20) { MPIX*=1.5; update_coords(); } else return 0; break;

		case ',':
		case '<':   if (MSIZE<WINDOW_XSIZE/(MPIX/1.5) || MSIZE<WINDOW_YSIZE/(MPIX/1.5)) return 0;
			     else  {MPIX/=1.5; update_coords(); break; } 
			
		case 'w':   GrSaveContextToPng(GrCurrentContext(),"output.png"); return 0;

  /* ctrl-f1..ctrl-f8 */
                       case 350: case 351: case 352:
                       case 353: case 354: case 355:
                       case 356: case 357:
        		 show_maps^=PORT_BIT(key-350+30); read_files(cur_file,0);
			  flago=1;
			  break;

		default:    return 0;
	}
	draw_map(1);
	return 0;
}


#ifdef QTGUI
int image_init(int argc,char *argv[]) {
#else
void main(int argc,char *argv[]) {
#endif
char temp[80];
int port_only=0;
char *wrk_path="paths";
char *image_cfg="image.cfg";
char *tiff_path=NULL;
char *info_path=NULL;

char portcfg[80];
char fname[80];
int i;
int server =0;
int lastflag;
FILE *out;
int hrs,mins,time=0;
int movie=0;
map_type want_id=MAP_1;   /* the product to open with */
int level;


for (i=1;i<argc;i++) {
  if (!strncmp(argv[i],"port",4)) {
   sscanf(argv[i],"port%d",&port_only);
   sprintf(portcfg,"image%d.cfg",port_only);
   image_cfg=portcfg;  //override center;
  } else if (!strncmp(argv[i],"map",3)) {
     /* mapN used to be a raw index into maps[], which meant something
      * different for every table.  The table is generated now, so N is the
      * constant altitude reflectivity level - the number the digit keys use. */
     sscanf(argv[i],"map%d",&level);
     if (level>=0 && level<=15) want_id=MAP_0+level;
  } else if (!strncmp(argv[i],"zdr",3)) {
     sscanf(argv[i],"zdr%d",&level);
     if (level>=1 && level<=10) want_id=MAP_D1+level-1;
  } else if (!strncmp(argv[i],"vel",3)) {
     sscanf(argv[i],"vel%d",&level);
     if (level>=1 && level<=10) want_id=MAP_V1+level-1;
  } else if (!strncmp(argv[i],"sum",3)) {
     sscanf(argv[i],"sum%d",&level);       /* hours: 1, 3, 6, 12 or 24 */
     want_id = level==24 ? MAP_Q24 : level==12 ? MAP_Q12 :
               level==6  ? MAP_Q6  : level==3  ? MAP_Q3  : MAP_Q1;
  } else if (!strcmp(argv[i],"top"))    want_id=MAP_H;
  else if (!strcmp(argv[i],"phenom"))   want_id=MAP_S;
  else if (!strcmp(argv[i],"rain"))     want_id=MAP_P;
  else if (!strncmp(argv[i],"paths",5)) wrk_path=argv[i];   //to allow paths1 paths2
  else if (!strncmp(argv[i],"server",6)) server =1 ;  //to skip grafs
  else if (!strncmp(argv[i],"time",4)) {
            sscanf(argv[i],"time%2d:%2d",&hrs,&mins);
	    time=1;
  } else if (!strncmp(argv[i],"info",4)) {
     char *eq=strchr(argv[i],'=');
     info_path = (eq && eq[1]) ? eq+1 : "output.json";
  } else if (!strncmp(argv[i],"geotiff",7)) {
     /* geotiff or geotiff=<file>: write the grid rather than a picture */
     char *eq=strchr(argv[i],'=');
     tiff_path = (eq && eq[1]) ? eq+1 : "output.tif";
  } else if (!strncmp(argv[i],"movie",5)) sscanf(argv[i],"movie%d",&movie);  //generate movie

}


 printf("IMAGE version 550 ot 22.05.2013\n");
 read_cfg(wrk_path,image_cfg);   /* builds maps[] */
 current_map=map_index(want_id);
 read_dir();

 /* Without this the file search below leaves cur_file at -1 and the drawing
  * code then walks uninitialised memory. */
 if (FilesRead <= 0) {
   printf("\n\aNo map files found in %s - check the MAP line in %s\n",
          mapdir, wrk_path);
   exit(1);
 }

if (server)  WINDOW_XSIZE=WINDOW_YSIZE=MSIZE*MPIX;


//load latest map for specific port;
lastflag=0;

if (time) {
  for (cur_file=FilesRead;cur_file--;cur_file>0) 
     if(Files[cur_file].FileMinute==mins && Files[cur_file].FileHour==hrs) break;
} else if (port_only)  {
  for (cur_file=FilesRead;cur_file--;cur_file>0) 
        if (Files[cur_file].flag & PORT_BIT(port_only-1)) {
            if (lastflag) break;  else lastflag=1;   //find previous
        }
  show_maps=PORT_BIT(port_only-1);
} else  {
  for (cur_file=FilesRead;cur_file--;cur_file>0) {
        if (lastflag && Files[cur_file].FileMinute%10==0) break;
        lastflag=1;     //find previous
  }
} 

 init_files();

 grafs=malloc(sizeof(struct info)*MaxTLO);
 if (grafs==NULL) exit(2);
 /* the geotiff carries data, not a picture - no geography on top of it */
 if (!port_only && !server && !tiff_path) set_cur_geogr(2);


do {
#if defined(GUI) || defined(QTGUI)
 read_files(cur_file,0);   /* interactive: load every product so the
                              product buttons can switch without a reload */
#else
 read_files(cur_file,1);   // 1-only current file
#endif

if (info_path && !tiff_path) {
   int ok;
   if (maps[current_map].mapid!=want_id) {
      fprintf(stderr,"this file carries no %s - nothing written to %s\n",
              maps[map_index(want_id)].filename,info_path);
      exit(1);
   }
   ok=save_info(info_path);
   de_init_files();
   free(grafs);
   exit(ok?0:1);
}

if (tiff_path) {
   /* Data out, not a picture: no window, no drawing, no overlay.
    *
    * set_cur_map() falls back to any product the file does carry, which is
    * what you want on screen and not at all what you want in a file named
    * after the product you asked for.  Say so and write nothing. */
   int ok;
   if (maps[current_map].mapid!=want_id) {
      fprintf(stderr,"this file carries no %s - nothing written to %s\n",
              maps[map_index(want_id)].filename,tiff_path);
      exit(1);
   }
   ok=save_geotiff(tiff_path);
   if (ok && info_path) ok=save_info(info_path);
   de_init_files();
   free(grafs);
   exit(ok?0:1);
}

if (server) {
 out=fopen("locators.js.cp866","w");
 fprintf(out,"var timestamp = \'%2d/%02d/%4d %2d:%02d\';\n",header[2],header[1],header[0]+2000,header[3],header[4]);
 fprintf(out,"var locators = [];\n");

 for (i=0;i<MaxPorts; i++) if (fk[current_map][i]) {

  float angle1,angle2;
  float x,y;

  conv_back(LU,BU,L0[i],B0[i],&x,&y);

  angle1=L0[i]*180/3.1415927;
  angle2=B0[i]*180/3.1415927;

  fprintf(out,"locators.push ({\"port\":%d,\"name\":\"%s\",",i,ST[i]);
  fprintf(out,"\"longlat\":\'%.2fe %.2fn\',",angle1,angle2);
  fprintf(out,"\"x\": %f,\"y\": %f});\n",x*1000,y*1000);

 }
 fclose(out);
}


 sprintf(temp,"%s/palette",grfdir);
 if (init_graph(temp)!=0) { exit(1); }

 draw_map(movie>0?0:1);


#if defined(QTGUI)
 /* Qt drives the event loop from here on */
#elif defined(GUI)
 GrSetWindowTitle("IMAGE");
 message_loop();
#else
if (port_only) { 
    sprintf(fname,"output%d.png",port_only);
    GrSaveContextToPng(RamContext,fname); 
//    sprintf(fname,"output%d.jpg",port_only);
//    GrSaveContextToJpeg(RamContext,fname,90); 
} else 
if (movie>0) {
  char  name[120];

  sprintf(name,"output%d.png",movie);
  GrSaveContextToPng(RamContext,name); 

} else {
   GrSaveContextToPng(RamContext,"output.png"); 
   GrSaveContextToPng(GrCurrentContext(),"table.png"); 
//   GrSaveContextToJpeg(RamContext,"output.jpg",90); 

   out=fopen("datetime.txt","w");
   fprintf(out,"%2d-%02d-%4d %2d:%02d",header[2],header[1],header[0]+2000,header[3],header[4]);
   fclose (out);
}

#endif

if (movie>0) { 
   movie--;
   cur_file--;
} 


} while (movie>0);

#ifdef QTGUI
 return 0;
#else
 close_graph();
 free(grafs);
 de_init_files();
#endif

 }
