#
# Makefile for the linux ext3-filesystem routines.
#

obj-$(CONFIG_EXT3_FS) += ext3.o

ext3-objs    := balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
		ioctl.o namei.o super.o symlink.o hash.o

include $(TOPDIR)/Rules.make
