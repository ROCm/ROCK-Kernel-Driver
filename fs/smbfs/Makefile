#
# Makefile for the linux smb-filesystem routines.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := smbfs.o

obj-y   := proc.o dir.o cache.o sock.o inode.o file.o ioctl.o getopt.o
obj-m   := $(O_TARGET)

# If you want debugging output, you may add these flags to the EXTRA_CFLAGS
# SMBFS_PARANOIA should normally be enabled.

EXTRA_CFLAGS += -DSMBFS_PARANOIA
#EXTRA_CFLAGS += -DSMBFS_DEBUG
#EXTRA_CFLAGS += -DSMBFS_DEBUG_VERBOSE
#EXTRA_CFLAGS += -DDEBUG_SMB_MALLOC
#EXTRA_CFLAGS += -DDEBUG_SMB_TIMESTAMP

include $(TOPDIR)/Rules.make
