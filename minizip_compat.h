/* Interface to the system minizip library.
 *
 * This project used to carry its own copy of minizip (unzip.c, ioapi.c and
 * friends).  Distributions ship that library now, so the sources were dropped
 * and only the declarations the project uses are kept here, for the case
 * where the minizip development headers are not installed.  The struct
 * layouts below are minizip's own; the .so is linked by make.sh.
 */

#ifndef MINIZIP_COMPAT_H
#define MINIZIP_COMPAT_H

#if defined(__has_include)
# if __has_include(<minizip/unzip.h>)
#  define HAVE_MINIZIP_HEADER 1
# endif
#endif

#ifdef HAVE_MINIZIP_HEADER

# include <minizip/unzip.h>

#else

#include <zlib.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UNZ_OK                  (0)
#define UNZ_END_OF_LIST_OF_FILE (-100)
#define UNZ_ERRNO               (Z_ERRNO)
#define UNZ_EOF                 (0)
#define UNZ_PARAMERROR          (-102)
#define UNZ_BADZIPFILE          (-103)
#define UNZ_INTERNALERROR       (-104)
#define UNZ_CRCERROR            (-105)

typedef voidp unzFile;

typedef struct tm_unz_s
{
    uInt tm_sec;                /* seconds after the minute - [0,59]  */
    uInt tm_min;                /* minutes after the hour - [0,59]    */
    uInt tm_hour;               /* hours since midnight - [0,23]      */
    uInt tm_mday;               /* day of the month - [1,31]          */
    uInt tm_mon;                /* months since January - [0,11]      */
    uInt tm_year;               /* years - [1980..2044]               */
} tm_unz;

typedef struct unz_global_info_s
{
    uLong number_entry;         /* entries in the central dir on this disk */
    uLong size_comment;         /* size of the global comment             */
} unz_global_info;

typedef struct unz_file_info_s
{
    uLong version;              /* version made by                 2 bytes */
    uLong version_needed;       /* version needed to extract       2 bytes */
    uLong flag;                 /* general purpose bit flag        2 bytes */
    uLong compression_method;   /* compression method              2 bytes */
    uLong dosDate;              /* last mod file date in Dos fmt   4 bytes */
    uLong crc;                  /* crc-32                          4 bytes */
    uLong compressed_size;      /* compressed size                 4 bytes */
    uLong uncompressed_size;    /* uncompressed size               4 bytes */
    uLong size_filename;        /* filename length                 2 bytes */
    uLong size_file_extra;      /* extra field length              2 bytes */
    uLong size_file_comment;    /* file comment length             2 bytes */

    uLong disk_num_start;       /* disk number start               2 bytes */
    uLong internal_fa;          /* internal file attributes        2 bytes */
    uLong external_fa;          /* external file attributes        4 bytes */

    tm_unz tmu_date;
} unz_file_info;

extern unzFile unzOpen(const char *path);
extern int unzClose(unzFile file);
extern int unzGetGlobalInfo(unzFile file, unz_global_info *pglobal_info);
extern int unzGoToFirstFile(unzFile file);
extern int unzGoToNextFile(unzFile file);
extern int unzLocateFile(unzFile file, const char *szFileName,
                         int iCaseSensitivity);
extern int unzGetCurrentFileInfo(unzFile file, unz_file_info *pfile_info,
                                 char *szFileName, uLong fileNameBufferSize,
                                 void *extraField, uLong extraFieldBufferSize,
                                 char *szComment, uLong commentBufferSize);
extern int unzOpenCurrentFile(unzFile file);
extern int unzCloseCurrentFile(unzFile file);
extern int unzReadCurrentFile(unzFile file, voidp buf, unsigned len);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_MINIZIP_HEADER */

#endif /* MINIZIP_COMPAT_H */
