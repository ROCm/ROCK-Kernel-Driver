#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.25 2001/09/25 20:59:41 dwmw2 Exp $
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...


COMPR_OBJS	:= compr.o compr_rubin.o compr_rtime.o pushpull.o \
			compr_zlib.o zlib.o
JFFS2_OBJS	:= crc32.o dir.o file.o ioctl.o nodelist.o malloc.o \
	read.o nodemgmt.o readinode.o super.o write.o scan.o gc.o \
	symlink.o build.o erase.o background.o

O_TARGET := jffs2.o

obj-y := $(COMPR_OBJS) $(JFFS2_OBJS)
obj-m := $(O_TARGET)

include $(TOPDIR)/Rules.make

