#include <stdlib.h>
#include <string.h>
#include <grx20.h>
#include "image.h"
#ifdef QTGUI
#include "qt_bridge.h"
#endif

extern long colors[NUM_COLORS];

win_save *OpenWindow(int CoordX,int CoordY,int SizeX,int SizeY,char *Header,int HeaderColor) {
win_save *SavePtr;
int OffsetX;
GrContext *save_context;
GrCursor  *mycursor;
GrColor cols[3];
static char ptr13x12[] = {

     0,0,0,0,0,0,2,0,0,0,0,0,0,
     0,0,0,0,0,2,2,2,0,0,0,0,0,
     0,0,0,0,0,2,2,2,0,0,0,0,0,
     0,0,0,0,2,2,1,2,2,0,0,0,0,
     0,0,0,0,2,1,1,1,2,0,0,0,0,
     0,0,0,2,2,1,1,1,2,2,0,0,0,
     0,0,0,2,1,1,1,1,1,2,0,0,0,
     0,0,2,2,1,1,1,1,1,2,2,0,0,
     0,0,2,1,1,1,1,1,1,1,2,0,0,
     0,2,2,1,1,1,1,1,1,1,2,2,0,
     2,2,1,1,1,1,1,1,1,1,1,2,2,
     2,2,2,2,2,2,2,2,2,2,2,2,2,
     
 };

/* Save initial picture */
	SavePtr=malloc(sizeof(win_save));
        SavePtr->was_cursor=GrMouseCursorIsDisplayed();
        GrMouseEraseCursor();
	SavePtr->x1=CoordX;
	SavePtr->y1=CoordY-4;
	SavePtr->x2=CoordX+SizeX;
	SavePtr->y2=CoordY+SizeY;
	save_context=GrCreateFrameContext(
          (GrCurrentVideoMode()->bpp==8)?GR_frameRAM8:GR_frameRAM4,
          SavePtr->x2-SavePtr->x1+1,SavePtr->y2-SavePtr->y1+1,NULL,NULL);
	GrBitBlt(save_context,0,0,(GrContext *)GrCurrentContext(),
              SavePtr->x1,SavePtr->y1,SavePtr->x2,SavePtr->y2,GrWRITE);
	SavePtr->context=save_context;

/* Draw Window */
	GrBox(CoordX,CoordY,CoordX+SizeX,CoordY+SizeY,colors[15]);

/* Clear it */
	GrFilledBox(CoordX+1,CoordY+1,CoordX+SizeX-1,CoordY+SizeY-1,colors[0]);

	if (strlen(Header)) {  /* Write header */
	    OffsetX=(SizeX-strlen(Header)*8)/2;
//	      GrFilledBox(CoordX+OffsetX-1,CoordY-4,CoordX+OffsetX+strlen(Header)*8,CoordY+4,colors[0]);
//            setcolor(HeaderColor);
	    outtextxy(CoordX+OffsetX,CoordY-4,Header);
	}
        cols[0] = 2;
        cols[1] = colors[10];
        cols[2] = colors[0];
        mycursor = GrBuildCursor(ptr13x12,13,13,12,6,1,cols);
        SavePtr->new_cursor=mycursor;
        return (SavePtr);
}

void CloseWindow(win_save *SavePtr) {
    GrDestroyCursor((GrCursor *)(SavePtr->new_cursor));
/* Restore initial picture */
    GrBitBlt((GrContext *)GrCurrentContext(),SavePtr->x1,SavePtr->y1,
     (GrContext *)(SavePtr->context),0,0,SavePtr->x2-SavePtr->x1+1,
                                    SavePtr->y2-SavePtr->y1+1,GrWRITE);
    GrDestroyContext((GrContext *)(SavePtr->context));
    if (SavePtr->was_cursor) GrMouseDisplayCursor();
    free(SavePtr);
}

void MoveCursor(win_save *SavePtr,int x,int y) { 
   GrMoveCursor((GrCursor *)(SavePtr->new_cursor),x,y);
   GrDisplayCursor((GrCursor *)(SavePtr->new_cursor));
}

void HideCursor(win_save *SavePtr) {
   GrEraseCursor((GrCursor *)(SavePtr->new_cursor));
}

int GetWindowKey(void) {
#ifdef QTGUI
   /* no GRX input device with the memory driver: Qt hands us the key */
   return qt_wait_key();
#else
GrMouseEvent event;
   do GrMouseGetEventT(GR_M_KEYPRESS | GR_M_NOPAINT,&event,1000);
   while (!(event.flags&GR_M_KEYPRESS));
   return event.key;
#endif
}
