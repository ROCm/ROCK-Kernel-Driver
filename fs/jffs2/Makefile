#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.34 2002/03/08 11:27:59 dwmw2 Exp $
#

obj-$(CONFIG_JFFS2_FS) += jffs2.o

jffs2-y	:= compr.o compr_rubin.o compr_rtime.o compr_zlib.o
jffs2-y	+= dir.o file.o ioctl.o nodelist.o malloc.o
jffs2-y	+= read.o nodemgmt.o readinode.o write.o scan.o gc.o
jffs2-y	+= symlink.o build.o erase.o background.o fs.o writev.o
jffs2-y	+= super.o

jffs2-$(CONFIG_JFFS2_FS_NAND)	:= wbuf.o
