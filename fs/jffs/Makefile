#
# Makefile for the linux Journalling Flash FileSystem (JFFS) routines.
#
# $Id: Makefile,v 1.7 2000/08/04 12:46:34 dwmw2 Exp $
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

ifndef CONFIG_JFFS_FS

# We're being invoked outside a normal kernel build. Fake it
EXTRA_CFLAGS= -I$(shell pwd)/../../include

# You need to change this to build for 2.2, dunno how to check for it.

#INODE_O := inode-v22.o
INODE_O := inode-v23.o

else

ifeq ($(PATCHLEVEL),2)
	INODE_O := inode-v22.o
else
	INODE_O := inode-v23.o
endif

endif

O_TARGET := jffs.o
obj-m    := $(O_TARGET)
obj-y    := jffs_fm.o intrep.o $(INODE_O)

include $(TOPDIR)/Rules.make
