#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "image.h"

#ifdef __TURBOC__
# include <io.h>
# define OPEN_MODE   O_DENYALL
#else
# include <unistd.h>
# define OPEN_MODE   0 // SH_DENYRW
#endif

#define COPYBUFSIZE 32768U

int move_file(char *from,char *to) {
int i,maxi;
char *bufptr;
int handle1;
FILE *file2;
long size;
struct stat buf;

  if (stat(from,&buf)!=0) return 1;
  size=buf.st_size;
  handle1=open(from,O_RDONLY | OPEN_MODE);
  if (handle1==-1) return 1;
  file2=fopen(to,"wb");
	if (file2==NULL) {
		 close(handle1);
		 return 1;
	}
  bufptr=malloc(COPYBUFSIZE);
  if (bufptr==NULL) {
	 printf("Not enough memory for copy!!!\n");
	 close(handle1);
	 fclose(file2);
	 return 1;
  }
  maxi=size/COPYBUFSIZE;
  if (maxi) for (i=0;i<maxi;i++) {
	  read(handle1,bufptr,COPYBUFSIZE);
	  fwrite(bufptr,1,COPYBUFSIZE,file2);
  }
  read(handle1,bufptr,size%COPYBUFSIZE);
  fwrite(bufptr,1,size%COPYBUFSIZE,file2);

  close(handle1);
  fclose(file2);
  free(bufptr);
  unlink(from);
  return 0;

}


struct MyFile Files[MaxFiles];
int FilesRead;

int n,CurDay;

/* For Quick sort */
int cmp(struct MyFile *a,struct MyFile *b) {
int result;
		   result = a->FileYear-b->FileYear;
      if (!result) result = a->FileMonth-b->FileMonth;   else return(result);
      if (!result) result = a->FileDay-b->FileDay;       else return(result);
      if (!result) result = a->FileHour-b->FileHour;     else return(result);
      if (!result) result = a->FileMinute-b->FileMinute;      return(result);
}

void read_dir(void) {
DIR *arc_dir;
struct dirent *entry;
char dir_name[100];
struct MyFile *TempFiles[MaxPorts];
int temp_files_read[MaxPorts],ip[MaxPorts],ip_min,min;
int i,j;

  for (i=0;i<MaxPorts;i++) {
    TempFiles[i]=malloc(sizeof(struct MyFile)*MaxFiles);
    temp_files_read[i]=0;
  }

  if (TempFiles[MaxPorts-1]==NULL) {
    printf("out of memory\n");
    exit(2);
  }

  for (i=0;i<MaxPorts;i++) {
     sprintf(dir_name,"%s/port%d",mapdir,i+1);
     arc_dir=opendir(dir_name);
     if (arc_dir==NULL) continue;
     j=0;
     while (j<MaxFiles && (entry = readdir(arc_dir)) != NULL)
       if (entry->d_name[0]!='.' && strlen(entry->d_name)==12) {
	      TempFiles[i][j].FileYear  =(entry->d_name[0]-'0')*10+entry->d_name[1]-'0';
	      TempFiles[i][j].FileMonth =(entry->d_name[2]-'0')*10+entry->d_name[3]-'0';
	      TempFiles[i][j].FileDay   =(entry->d_name[4]-'0')*10+entry->d_name[5]-'0';
	      TempFiles[i][j].FileHour  =(entry->d_name[6]-'0')*10+entry->d_name[7]-'0';
	      TempFiles[i][j].FileMinute=(entry->d_name[9]-'0')*10+entry->d_name[10]-'0';
	      j++;
     }
     closedir(arc_dir);
     temp_files_read[i]=j;
     if (j) qsort((void *)TempFiles[i],j,sizeof(struct MyFile),(int(*)(const void*,const void*))cmp);
  }

  FilesRead=0;
  for (i=0;i<MaxPorts;i++) if (temp_files_read[i]) ip[i]=0; else ip[i]=-1;
  j=0;
  while(1) {
      for (min=0;min<MaxPorts;min++) if(ip[min]!=-1) break;
			if (min==MaxPorts) break;  /* end of loop */
      for (i=min+1;i<MaxPorts;i++)
	if (ip[i]!=-1 && cmp(&TempFiles[min][ip[min]],&TempFiles[i][ip[i]])>0) min=i;
      memcpy(&Files[j],&TempFiles[min][ip[min]],sizeof(struct MyFile));
      ip_min=ip[min];
      Files[j].flag=0;
      for (i=0;i<MaxPorts;i++)
	if (ip[i]!=-1 && !cmp(&TempFiles[min][ip_min],&TempFiles[i][ip[i]])) {
	   Files[j].flag|=1LL<<i;
	   if (++ip[i]==temp_files_read[i]) ip[i]=-1;
      }
      if (++j==MaxFiles) {
	j=0;
	FilesRead=MaxFiles;
      }
  }
  if (FilesRead==0) FilesRead=j;
  if (FilesRead)
     qsort(Files,FilesRead,sizeof(struct MyFile),(int(*)(const void*,const void*))cmp);

  for (i=0;i<MaxPorts;i++) free(TempFiles[i]);
  n=-1;
  CurDay=-1;
}

#define WindowX 100  //120
#define WindowY 100
#define WindowSizeX 700  //500
#define WindowSizeY 170       //120  /* 70 */

#define YearY 10

#define BeginX 5
#define BeginY 30

#define CursorSizeX 3
#define CursorSizeY 8

/* For lines for each day */
#define LineLength WindowSizeY-BeginY-YearY
#define UpperOffset 14
#define LowerOffset 15
#define LineSpace 2
#define EachLine (LineLength-LowerOffset-CursorSizeY-(MaxPorts-1)*LineSpace)/MaxPorts

#define CursorY WindowSizeY-2*BeginY-CursorSizeY-3

#define MessageX 50
#define MessageY WindowSizeY-15

#define Window2X     WindowX+5
#define Window2Y     WindowY+WindowSizeY
#define Window2SizeX WindowSizeX-10
#define Window2SizeY 150  //100 /* 40 */

#define Cursor2SizeX 3
#define Cursor2SizeY 6

#define Begin2X 5
#define Begin2Y 3

#define LineLength2 Window2SizeY-2*Begin2Y-5
#define UpperOffset2 13
#define LowerOffset2 12
#define LineSpace2 2
#define EachLine2 (LineLength2-LowerOffset2-Cursor2SizeY-(MaxPorts-1)*LineSpace2)/MaxPorts

#define Cursor2Y Window2SizeY-2*Begin2Y-Cursor2SizeY-3

enum COLORS {
    BLACK,                  /* dark colors */
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    BROWN,
    LIGHTGRAY,
    DARKGRAY,               /* light colors */
    LIGHTBLUE,
    LIGHTGREEN,
    LIGHTCYAN,
    LIGHTRED,
    LIGHTMAGENTA,
    YELLOW,
    WHITE
};

char months[][10]={"Январь","Февраль","Март","Апрель","Май","Июнь",
        "Июль","Август","Сентябрь","Октябрь","Ноябрь","Декабрь" };

char months2[][4]={"Янв","Фев","Мар","Апр","Май","Июн",
        "Июл","Авг","Сен","Окт","Ноя","Дек" };

#define Left  331
#define Right 333
#define Up    328
#define Down  336
#define Home  327
#define End   335
#define F2    316
#define F3    317
#define Enter  13
#define Escape 27

int anim_begin,anim_end;
void MessageLine(int);
int DayWindow(win_save *MainWindowPtr,int i,int month_width);

int archive (void) {

win_save *WindowPtr;
int i,j,k,flag,month_width,no_months,ofst,ch;
char string[80];
 flaga=1;
	WindowPtr=OpenWindow(WindowX,WindowY,WindowSizeX,WindowSizeY,"Архив",RED);
	if (!FilesRead) return 0;
	if (n==-1) n=FilesRead-1;

do {
	flag=0;
	anim_begin=anim_end=-1;
/* Clear window */
	solid_bar(WindowX+1,WindowY+5,WindowX+WindowSizeX-1,WindowY+WindowSizeY-1,0);
/* Find first file with the last year & store to i */
	i=0;
	do ; while (Files[i++].FileYear<Files[n].FileYear); i--;
	no_months=Files[n].FileMonth-Files[i].FileMonth+1;
	month_width=(WindowSizeX-BeginX*2)/no_months;
	if (CurDay==-1) CurDay=n-i;
	/* printf("\n %i n=%i",CurDay,n);*/
/* Print year number */
	sprintf(string,"%4d год",Files[i].FileYear+2000);
	outtextxy(WindowX+(WindowSizeX-strlen(string)*8)/2,WindowY+YearY,string);

	current_color=RED;
/* Draw lines and names of months */
	for (j=0;j<=no_months*month_width;j+=month_width)
				my_line(WindowX+BeginX+j,WindowY+BeginY,WindowX+BeginX+j,WindowY+BeginY+LineLength);
	my_line(WindowX+BeginX,WindowY+BeginY,
		   WindowX+BeginX+no_months*month_width,WindowY+BeginY);
	my_line(WindowX+BeginX,WindowY+BeginY+LineLength,
		   WindowX+BeginX+no_months*month_width,WindowY+BeginY+LineLength);
	/*	printf("\n Cur=%i n=%i, i=%i,j=%i,m=%i",CurDay,n,i,j,month_width);*/
	current_color=GREEN;
	for (j=Files[i].FileMonth;j<=Files[n].FileMonth;j++)
		if (month_width>strlen(months[j-1])*8)
		  outtextxy(WindowX+BeginX+(j-Files[i].FileMonth)*month_width+
		    (month_width-strlen(months[j-1])*8)/2,WindowY+BeginY+4,months[j-1]);
		else
		  outtextxy(WindowX+BeginX+(j-Files[i].FileMonth)*month_width+
		    (month_width-strlen(months2[j-1])*8)/2,WindowY+BeginY+4,months2[j-1]);


/* Draw lines for each day */
	current_color=BLUE;
	for (j=0;j<=n-i;j++) {
		ofst = (Files[i+j].FileMonth-Files[i].FileMonth)*month_width+1+
				 (Files[i+j].FileDay-1)*(month_width-2)/30;
		for (k=0;k<MaxPorts;k++) if (Files[i+j].flag & (1LL<<k))
		   my_line(WindowX+BeginX+ofst,WindowY+BeginY+UpperOffset+(LineSpace+EachLine)*k,
			 WindowX+BeginX+ofst,WindowY+BeginY+UpperOffset+(LineSpace+EachLine)*k+EachLine);
	}

	do {
            MessageLine(i);
	    ofst = (Files[i+CurDay].FileMonth-Files[i].FileMonth)*month_width+1+
			     (Files[i+CurDay].FileDay-1)*(month_width-2)/30;

/* Draw cursor */
	    MoveCursor(WindowPtr,WindowX+BeginX+ofst,WindowY+BeginY+CursorY);

			ch=GetWindowKey();
			switch (ch) {
		  case Down  : ch=Enter;
		  case Enter : if (DayWindow(WindowPtr,i,month_width)) flag=1; break;
		  case Home  : CurDay=0; break;
		  case End   : CurDay=n-i; break;
		  case Left  : if (CurDay) do CurDay--; while (Files[i+CurDay].FileDay==Files[i+CurDay+1].FileDay &&
							  CurDay && Files[i+CurDay].FileMonth==Files[i+CurDay+1].FileMonth);
					   else flag=1; break;
		  case Right : if (CurDay<n-i) do CurDay++; while (Files[i+CurDay].FileDay==Files[i+CurDay-1].FileDay &&
							 CurDay<n-i &&  Files[i+CurDay].FileMonth==Files[i+CurDay-1].FileMonth);
					   else flag=1; break;
	    } /*switch */
	} while (ch!=Escape && !flag);
	n++;
	if (flag && ch==Left && i) {n=i;CurDay=-1;}
	if (flag && ch==Right && n<FilesRead) {do n++; while (Files[i+CurDay+1].FileYear==
			 Files[n].FileYear);CurDay=0;}
	n--;
	HideCursor(WindowPtr);
	} while (ch!=Escape && ch!=Enter && !((ch==F3 || ch==F2) && anim_begin && anim_end));
  CloseWindow(WindowPtr);
	if (ch==Enter) return i+CurDay; else return -1;
}

int DayWindow(win_save *MainWindowPtr,int i,int month_width) {
win_save *WindowPtr;
int no_hours,hour_width,j,k,ofst,flag;
int ch;
int FirstDay,LastDay;
char s5[80];

		WindowPtr=OpenWindow(Window2X,Window2Y,Window2SizeX,Window2SizeY,"",0);
		do {
/* Clear Window */
	solid_bar(Window2X+1,Window2Y+1,Window2X+Window2SizeX-1,Window2Y+Window2SizeY-1,0);
	flag=0;
	FirstDay=LastDay=CurDay;
	do FirstDay--; while (FirstDay>=0 && Files[i+FirstDay].FileMonth==Files[i+CurDay].FileMonth &&
			Files[i+FirstDay].FileDay==Files[i+CurDay].FileDay); FirstDay++;
	do LastDay++; while (LastDay<=n-i && Files[i+LastDay].FileMonth==Files[i+CurDay].FileMonth &&
			Files[i+LastDay].FileDay==Files[i+CurDay].FileDay); LastDay--;
	no_hours=Files[i+LastDay].FileHour-Files[i+FirstDay].FileHour+1;
	hour_width=(Window2SizeX-Begin2X*2)/no_hours;
/*	 printf("\n F%i,L%i,D%i,i-%i,n-%i,FR-%i",FirstDay,LastDay,CurDay,i,n,FilesRead);*/
/* Draw boxes for each hour */
	current_color=14;
	for (j=0;j<=no_hours*hour_width;j+=hour_width)
		my_line(Window2X+Begin2X+j,Window2Y+Begin2Y,Window2X+Begin2X+j,Window2Y+Begin2Y+LineLength2);
	my_line(Window2X+Begin2X,Window2Y+Begin2Y,
		 Window2X+Begin2X+no_hours*hour_width,Window2Y+Begin2Y);
	my_line(Window2X+Begin2X,Window2Y+Begin2Y+LineLength2,
		 Window2X+Begin2X+no_hours*hour_width,Window2Y+Begin2Y+LineLength2);

/* Draw hour numbers */
	current_color=8;
	for (j=Files[i+FirstDay].FileHour;j<=Files[i+LastDay].FileHour;j++) {
		sprintf(s5,"%d",j);
		outtextxy(Window2X+Begin2X+(j-Files[i+FirstDay].FileHour)*hour_width+
		    (hour_width-strlen(s5)*8)/2,Window2Y+Begin2Y+4,s5);
	}

/* Draw lines for each minute */
	current_color=7;
	for (j=FirstDay;j<=LastDay;j++) {
		ofst = (Files[i+j].FileHour-Files[i+FirstDay].FileHour)*hour_width+1+
			 (Files[i+j].FileMinute)*(hour_width-2)/59;

		for (k=0;k<MaxPorts;k++) if (Files[i+j].flag & (1LL<<k)) {
//				if (Files[i+j].SummFlag & (1<<k)) current_color=7; else current_color=BLUE;
				my_line(Window2X+Begin2X+ofst,
			 Window2Y+Begin2Y+UpperOffset2+(LineSpace2+EachLine2)*k,
			 Window2X+Begin2X+ofst,
			 Window2Y+Begin2Y+UpperOffset2+(LineSpace2+EachLine2)*k+EachLine2);
		 }
	}
	do {
          MessageLine(i);
          ofst = (Files[i+CurDay].FileHour-Files[i+FirstDay].FileHour)*hour_width+1+
			 (Files[i+CurDay].FileMinute)*(hour_width-2)/59;
/* Draw cursor */
           MoveCursor(WindowPtr,Window2X+Begin2X+ofst,Window2Y+Begin2Y+Cursor2Y);

	    ch=GetWindowKey();
	    switch (ch) {
                  case Up    : ch=Escape; break;
		  case Left  : if (CurDay)  CurDay--;   if (CurDay<FirstDay) flag=1; break;
		  case Right : if (CurDay<n-i)CurDay++; if (CurDay>LastDay)  flag=1; break;
		  case Home  : CurDay=FirstDay; break;
		  case End   : CurDay=LastDay;  break;
		  case F2    : anim_begin=i+CurDay;break;
		  case F3    : anim_end = i+CurDay;break;

	    } /*switch */
	} while (!flag && ch!=Escape && ch!=Enter && !((ch==F3 || ch==F2) && anim_begin!=-1 && anim_end!=-1));
     HideCursor(WindowPtr);
		 ofst = (Files[i+CurDay].FileMonth-Files[i].FileMonth)*month_width+1+
			     (Files[i+CurDay].FileDay-1)*(month_width-2)/30;
		 MoveCursor(MainWindowPtr,WindowX+BeginX+ofst,WindowY+BeginY+CursorY);
 } while (flag);
    if (anim_end!=-1 && anim_begin>anim_end) { j=anim_begin; anim_begin=anim_end; anim_end=j; }
    CloseWindow(WindowPtr);
    MessageLine(i);
    return((ch==Enter || ch==F2 || ch==F3 || ch==0)? 1:0);
}

void MessageLine(int i) {
char s5[80];

//  solid_bar(WindowX+MessageX,WindowY+MessageY,WindowX+MessageX+MaxChars*8,WindowY+MessageY+8,0);
  sprintf(s5,"ALL PORTS: Размер %3dK; Дата %02d/%02d; Время %02d:%02d",
//	  Files[i+CurDay].FileSize,
              0,Files[i+CurDay].FileDay,Files[i+CurDay].FileMonth,
              Files[i+CurDay].FileHour,Files[i+CurDay].FileMinute);
  outtextxy(WindowX+MessageX,WindowY+MessageY,s5);
}


void ViewString(int x,int y,int size_x,int size_y,char *line[]) {
int i;
win_save *Window;
int StringX,StringY;

StringX=x*CharXSize;
StringY=y*CharYSize;

Window=OpenWindow(StringX-2,StringY-2,size_x*CharXSize+3,
                size_y*CharYSize+3,"",0);
for (i=0;i<size_y;i++) outtextxy(StringX,StringY+i*CharYSize,line[i]);
getch();
CloseWindow(Window);

}
