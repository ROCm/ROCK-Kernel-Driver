#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.11 2001/09/25 20:59:41 dwmw2 Exp $
#

obj-y := jffs_fm.o intrep.o

ifeq ($(PATCHLEVEL),2)
	obj-y += inode-v22.o
else
	obj-y += inode-v23.o
endif

ifeq ($(CONFIG_JFFS_PROC_FS),y)
	obj-y += jffs_proc.o
endif

O_TARGET := jffs.o

obj-m := $(O_TARGET)

include $(TOPDIR)/Rules.make

