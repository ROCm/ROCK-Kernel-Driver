#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.21 2001/03/25 22:36:12 dwmw2 Exp $
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

ifndef CONFIG_JFFS2_FS

CC += -I$(shell pwd)/../../include

obj-m := jffs2.o comprmod.o
# We're being invoked outside a normal kernel build. Fake it
EXTRA_CFLAGS= -DCONFIG_JFFS2_FS_DEBUG=1 -g
endif

obj-$(CONFIG_JFFS2_FS)	+= jffs2.o

COMPR_OBJS	:= compr.o compr_rubin.o compr_rtime.o pushpull.o \
			compr_zlib.o zlib.o
JFFS2_OBJS	:= crc32.o dir.o file.o ioctl.o nodelist.o malloc.o \
	read.o nodemgmt.o readinode.o super.o write.o scan.o gc.o \
	symlink.o build.o erase.o background.o

jffs2-objs	:= $(COMPR_OBJS) $(JFFS2_OBJS)
comprmod-objs	:= $(COMPR_OBJS) comprtest.o

jffs2.o: $(jffs2-objs)
	$(LD) -r -o $@ $(jffs2-objs)

comprmod.o: $(comprmod-objs)
	$(LD) -r -o $@ $(comprmod-objs)

include $(TOPDIR)/Rules.make
