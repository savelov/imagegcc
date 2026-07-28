# imagegcc
requires GRX2.4.9 http://grx.gnu.de/download/index.html
bufr2wrk.py - script to process BUFR files
requires archive with zip files portN/YYMMDDHH.MMm

requires belo patch in grx library

--- vd_mem.c.orig-backup	2026-07-28 08:12:58.138735696 +0200
+++ vd_mem.c	2026-07-28 08:12:58.160524509 +0200
@@ -109,7 +109,7 @@
     NULL,                               /* frame driver override */
     NULL,                               /* frame buffer address */
     { 8, 8, 8 },                        /* color precisions */
-    { 0, 0, 0 },                        /* color component bit positions */
+    { 16, 8, 0 },                       /* color component bit positions */
     GR_VMODEF_MEMORY,                   /* mode flag bits */
     mem_setmode,                        /* mode set */
     NULL,                               /* virtual size set */
