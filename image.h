/* Header file for New Image project */
/* Compiles under Borland C, Djgpp C, and Linux */

#ifdef __TURBOC__
# ifndef __FLAT__
#   define _HUGE huge
#   define _MALLOC farmalloc
# else
#   define _HUGE
#   define _MALLOC malloc
# endif
#else
# define _HUGE
# define _MALLOC malloc
#endif

#include <inttypes.h>


//#define GUI

#define NUM_COLORS 16        /* number of custom reserved colors */
#define MaxFiles 200000        /* max files in directory */
#define MaxPorts 99

/* One bit per port.  Up to 63 ports fitted in a 64 bit word; beyond that the
 * mask needs a wider type, and gcc's __int128 carries 128 of them. */
typedef unsigned __int128 port_mask;
#define PORT_BIT(p)  ((port_mask)1 << (p))
#define PORT_ALL     (~(port_mask)0)
#define PMSG 16
#define c10 10

#define MAX_Y 16
#define MAX_X 69
#define T_CHARS 8

#define STORM_X 60
#define STORM_Y 13
#define cof 6
#define Rsv 150
#define Rst 160
typedef enum map_types {
 MAP_0,  MAP_1,  MAP_2,  MAP_3,  MAP_4,
 MAP_5,  MAP_6,  MAP_7,  MAP_8,  MAP_H,
 MAP_S,  MAP_Q,  MAP_P
} map_type;

struct map_info {
	map_type mapid;
	char *filename;
	char *thresh;
	char *descr;
	float mapres;      /* init by scan_maps() */
	unsigned char *bufhead;   /* init by scan_maps() */
	unsigned char *bufdata;   /* init by scan_maps() */
};

extern int current_map,cur_file;
extern struct map_info maps[];
extern char grfdir[],mapdir[],cfgdir[];
extern float LC,BC,LU,BU;
extern unsigned char header[128];
extern char current_prefix;
extern unsigned int MSIZE;
extern float MPIX;
extern int masx[MaxPorts],masy[MaxPorts];
extern unsigned char _HUGE *mapbuffer;
extern float L0[MaxPorts],B0[MaxPorts];
extern void init_files(void);
extern void de_init_files(void);
extern void set_cur_map(map_type id);
extern void read_files(int map_number,int flag);
extern void read_cfg(char *wrk_path,char *image_cfg);
extern int get_map_data(map_type id,int xcoord,int ycoord);

extern void showtime(void);
extern void showdata(int x,int y);
extern void show_header(void);

extern int current_color;

extern int WINDOW_XSIZE,WINDOW_YSIZE,WINDOW_LEFT,WINDOW_UP;

extern void message_loop(void);
extern int message_poll(void);
extern int init_graph(char *palette_file);
extern void close_graph(void);
extern int draw_map(int vectors);
extern void outtextxy(int x,int y,char *text);
extern void my_circle(int x_center,int y_center,int radius);
extern void my_pixel(int x,int y);
extern void my_line(int x1,int y1,int x2,int y2);
extern void my_poly(int numpoints,int  polypoints[]);
extern void my_textxy(int x_begin,int y_begin,char text[]);
extern void solid_bar(int x1,int y1,int x2,int y2,int color);

#define RAD   M_PI/180.
#define GR    180./M_PI

extern void convert(float LA,float BA,float X,float Y,float *Lar,float *Fir);
extern void convert_port(float LA,float BA,float X,float Y,float *Lar,float *Fir);
extern void conv_back(float LA,float BA,float Lar,float Fir,float *X,float *Y);
extern unsigned char *get_ptr(float LN,float BN,float MapRes,int size_from,int size_to);
extern void koord(unsigned char *tb,unsigned char *tbl_ptr, unsigned int size_from,int port);
extern void walk_table(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to);
extern void walk_table0(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,float MapRes,int port,int);
extern void walk_tables(unsigned char *tb,unsigned char *ntb,unsigned char *tbl_ptr,unsigned int size_from,unsigned int size_to,float MapRes,int port,int);
extern void interpolation (unsigned char *ntb,int map_size);
extern void expand(unsigned char *in,unsigned char _HUGE *out,int mapsize);
extern void expand_heights(unsigned char *in,unsigned char _HUGE *out,int mapsize);
struct info {
  short type;
  short x1,x2,y1,y2;
  unsigned int size;
  void *ptr;
};

#define MaxTLO 3000

void draw_tlo(struct info inf_ptr[],int max_number);
void set_cur_geogr(int number);

extern struct    info *grafs;
extern int       max_grafs;

extern int move_file(char *from,char *to);
extern void read_dir(void);

struct MyFile {                             /* For an archive */
  unsigned char FileYear;
  unsigned char FileMonth;
  unsigned char FileDay;
  unsigned char FileHour;
  unsigned char FileMinute;
  port_mask flag;
};
extern struct MyFile Files[MaxFiles];
extern int FilesRead;

extern int              Second;
extern char             in_dir[MaxPorts][100];

/* Callback functions */
extern int              key_pressed(int key);
extern void mouse_move(int x,int y);
extern int  mouse_click_left(int x,int y);
extern void mouse_dclick_left(int x, int y);
extern int  mouse_click_right(int x,int y);
extern void timer(void);


typedef struct save {
        void *context;
        int x1,y1,x2,y2;
        int was_cursor;
        void *new_cursor;
} win_save;


win_save *OpenWindow(int CoordX,int CoordY,int SizeX,int SizeY,char *Header,int HeaderColor);
void CloseWindow(win_save *SavePtr);
void MoveCursor(win_save *SavePtr,int x,int y);
void HideCursor(win_save *SavePtr);
int GetWindowKey(void);

extern int archive(void);
extern int anim_begin,anim_end;

#define CharXSize 8
#define CharYSize 16

extern void ViewString(int x,int y,int size_x,int size_y,char *line[]);

extern void ViewForecast(int number);

extern port_mask show_maps;


extern unsigned char stormmsg[PMSG][c10];
extern char palmsg[PMSG][c10];
extern int sto[15],cu;
extern unsigned int MSIZE_int;
extern long clev[256];
extern int y_up,x_left,xck,yck,xc1,yc1,xc2,yc2;
extern int xco,yco,xco1,xco2,yco1,yco2;
extern int flagl,flagv,flags,flago,flaga,fp;
extern float MRES,BW,HMRL[MaxPorts];
extern float SPEED[MaxPorts],W[MaxPorts][20];
extern signed int AZIMUT[MaxPorts],R[MaxPorts];
extern char ST[MaxPorts][50];
extern int Sezon;
extern void set_cur_prefix(int cur_file,char prefix);
extern void vert(int x1,int y1,int x2,int y2);
extern int fk[25][MaxPorts];

extern int getch(void);