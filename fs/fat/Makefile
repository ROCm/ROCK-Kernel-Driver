#
# Makefile for the Linux fat filesystem support.
#

export-objs := fatfs_syms.o

obj-$(CONFIG_FAT_FS) += fat.o

fat-objs := buffer.o cache.o dir.o file.o inode.o misc.o cvf.o fatfs_syms.o

include $(TOPDIR)/Rules.make
