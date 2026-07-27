/* Replacements for the Borland/TurboC runtime calls this project still uses. */

/* image.h must come first: it declares B0[], which <termios.h> would
 * otherwise have turned into a baud rate macro. */
#include "image.h"

#include <stdio.h>
#include <unistd.h>
#include <termios.h>

/* conio.h getch(): read one key without waiting for Enter and without echo.
 * Used on the error paths to hold the message on screen before exit.  When
 * stdin is not a terminal it degrades to a plain read, so the batch build
 * does not stall. */
int getch(void)
{
    struct termios saved, raw;
    int c;

    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved) != 0)
        return getchar();

    raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    c = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return c;
}
