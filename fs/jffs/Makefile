#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.11 2001/09/25 20:59:41 dwmw2 Exp $
#

jffs-objs := jffs_fm.o intrep.o

ifeq ($(PATCHLEVEL),2)
	jffs-objs += inode-v22.o
else
	jffs-objs += inode-v23.o
endif

ifeq ($(CONFIG_JFFS_PROC_FS),y)
	jffs-objs += jffs_proc.o
endif

O_TARGET := jffs.o

obj-y := $(jffs-objs)
obj-m := $(O_TARGET)

include $(TOPDIR)/Rules.make

