#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.34 2002/03/08 11:27:59 dwmw2 Exp $
#

obj-$(CONFIG_JFFS2_FS) += jffs2.o

COMPR_OBJS	:= compr.o compr_rubin.o compr_rtime.o compr_zlib.o
JFFS2_OBJS	:= dir.o file.o ioctl.o nodelist.o malloc.o \
	read.o nodemgmt.o readinode.o write.o scan.o gc.o \
	symlink.o build.o erase.o background.o fs.o writev.o

LINUX_OBJS-24	:= super-v24.o crc32.o
LINUX_OBJS-25	:= super.o
LINUX_OBJS-26	:= super.o

NAND_OBJS-$(CONFIG_JFFS2_FS_NAND)	:= wbuf.o

jffs2-objs := $(COMPR_OBJS) $(JFFS2_OBJS) $(VERS_OBJS) $(NAND_OBJS-y) \
	      $(LINUX_OBJS-$(VERSION)$(PATCHLEVEL))
