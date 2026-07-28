
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include "image.h"
#include <grx20.h>

extern GrContext *RamContext;

int cur_file;
int64_t show_maps=0xFFFFFFFFFFFFFFFF;

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

int animate (int begin,int end) {
int i;
int key;

for (i=begin; i<end; i++) {
   read_files(i,1);
   key=draw_map(1);
   showtime();
  if (key==' ') break;

  switch(key)  {
                case '0':  set_cur_map(MAP_0); break;
		case '1':  set_cur_map(MAP_1); break;
		case '2':  set_cur_map(MAP_2); break;
		case '3':  set_cur_map(MAP_3); break;
		case '4':  set_cur_map(MAP_4); break;
		case '5':  set_cur_map(MAP_5); break;
		case '6':  set_cur_map(MAP_6); break;
		case '7':  set_cur_map(MAP_7); break;
		case '8':  set_cur_map(MAP_8); break;
		case 'p':
		case 'P':  set_cur_map(MAP_P); break;
		case 'h':
		case 'H':  set_cur_map(MAP_H); break;
		case 's':
		case 'S':  set_cur_map(MAP_S); break;
		case 'q':
		case 'Q':  set_cur_map(MAP_Q); break;

                default : break;
    }
 }

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

int xcoord,ycoord;
	  xcoord=x-WINDOW_LEFT-WINDOW_XSIZE/2;
	  ycoord=y-WINDOW_UP-WINDOW_YSIZE/2;
	  if (xcoord>=0) xcoord=xcoord/MPIX+1; else xcoord=xcoord/MPIX-1;
	  if (ycoord>=0) ycoord=ycoord/MPIX+1; else ycoord=ycoord/MPIX-1;
	  showdata(xcoord,ycoord);

          if (xcoord>0) xcoord--;
          if (ycoord>0) ycoord--;
          xco=MSIZE/2+xcoord+x_left;
          yco=MSIZE/2+ycoord+y_up;
          if (xco<0) xco=0;
          if (yco<0) yco=0;

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
          xcoord=(x-WINDOW_LEFT-WINDOW_XSIZE/2)/MPIX;
					ycoord=(y-(WINDOW_UP)-WINDOW_YSIZE/2)/MPIX;
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
int xcoord,ycoord;
	xc1=x;yc1=y;
	  xcoord=x-WINDOW_LEFT-WINDOW_XSIZE/2;
	  ycoord=y-WINDOW_UP-WINDOW_YSIZE/2;
	  if (xcoord>=0) xcoord=xcoord/MPIX+1; else xcoord=xcoord/MPIX-1;
	  if (ycoord>=0) ycoord=ycoord/MPIX+1; else ycoord=ycoord/MPIX-1;
	  showdata(xcoord,ycoord);

          if (xcoord>0) xcoord--;
          if (ycoord>0) ycoord--;
          xco=MSIZE/2+xcoord+x_left;
          yco=MSIZE/2+ycoord+y_up;
          if (xco<0) xco=0;
          if (yco<0) yco=0;

	  xck=x; yck=y;
	  GrLine(xc1,yc1,xck,yck,colors[15]);
	 /* return 0;*/
}

int key_pressed(int key) { /* Returns 0 if OK to continue, 1 if Quit */
int result;
int min_left=WINDOW_XSIZE/MPIX/2+1-MSIZE/2;
int max_right=MSIZE/2-WINDOW_XSIZE/MPIX/2-1;
int min_up=WINDOW_YSIZE/MPIX/2+1-MSIZE/2;
int max_down=MSIZE/2-WINDOW_YSIZE/MPIX/2-1;


// printf ("key=%d\n",key);

	/*	printf("\n =%i",key);*/
	switch (key)
		{

//		case 363:  return 1;
		case 27:   return 1;
		case '0':  set_cur_map(MAP_0); break;
		case '1':  set_cur_map(MAP_1); break;
		case '2':  set_cur_map(MAP_2); break;
		case '3':  set_cur_map(MAP_3); break;
		case '4':  set_cur_map(MAP_4); break;
		case '5':  set_cur_map(MAP_5); break;
		case '6':  set_cur_map(MAP_6); break;
		case '7':  set_cur_map(MAP_7); break;
		case '8':  set_cur_map(MAP_8); break;
		case 'p':
		case 'P':  set_cur_map(MAP_P); break;
		case 'h':
		case 'H':  set_cur_map(MAP_H); break;
		case 's':
		case 'S':  set_cur_map(MAP_S); break;
		case 'q':
		case 'Q':  set_cur_map(MAP_Q); break;
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
        		 show_maps^=1LL<<(key-350+30); read_files(cur_file,0);
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

char portcfg[80];
char fname[80];
int i;
int server =0;
int lastflag;
FILE *out;
int hrs,mins,time=0;
int movie=0;


for (i=1;i<argc;i++) {
  if (!strncmp(argv[i],"port",4)) {
   sscanf(argv[i],"port%d",&port_only);
   sprintf(portcfg,"image%d.cfg",port_only);
   image_cfg=portcfg;  //override center;
  } else if (!strncmp(argv[i],"map",3))  sscanf(argv[i],"map%d",&current_map);
  else if (!strncmp(argv[i],"paths",5)) wrk_path=argv[i];   //to allow paths1 paths2
  else if (!strncmp(argv[i],"server",6)) server =1 ;  //to skip grafs
  else if (!strncmp(argv[i],"time",4)) {
            sscanf(argv[i],"time%2d:%2d",&hrs,&mins);
	    time=1;
  } else if (!strncmp(argv[i],"movie",5)) sscanf(argv[i],"movie%d",&movie);  //generate movie

}


 printf("IMAGE version 550 ot 22.05.2013\n");
 read_cfg(wrk_path,image_cfg);
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
        if (Files[cur_file].flag & (1LL<<(port_only-1))) {
            if (lastflag) break;  else lastflag=1;   //find previous
        }
  show_maps=1LL<<(port_only-1);
} else  {
  for (cur_file=FilesRead;cur_file--;cur_file>0) {
        if (lastflag && Files[cur_file].FileMinute%10==0) break;
        lastflag=1;     //find previous
  }
} 

 init_files();

 grafs=malloc(sizeof(struct info)*MaxTLO);
 if (grafs==NULL) exit(2);
 if (!port_only && !server) set_cur_geogr(2);


do {
#if defined(GUI) || defined(QTGUI)
 read_files(cur_file,0);   /* interactive: load every product so the
                              product buttons can switch without a reload */
#else
 read_files(cur_file,1);   // 1-only current file
#endif

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
