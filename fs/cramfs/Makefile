#
# Makefile for the linux cramfs routines.
#

O_TARGET := cramfs.o

obj-y  := inode.o uncompress.o inflate/zlib.o

obj-m := $(O_TARGET)

include $(TOPDIR)/Rules.make

inflate/zlib.o:
	make -C inflate
