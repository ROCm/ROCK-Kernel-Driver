#
# Makefile for the linux ext2-filesystem routines.
#

obj-$(CONFIG_EXT2_FS) += ext2.o

ext2-objs := balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
	     ioctl.o namei.o super.o symlink.o

include $(TOPDIR)/Rules.make
