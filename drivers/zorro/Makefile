#
# Makefile for the Zorro bus specific drivers.
#

export-objs		:= zorro.o

obj-$(CONFIG_ZORRO)	+= zorro.o names.o
obj-$(CONFIG_PROC_FS)	+= proc.o

host-progs 		:= gen_devlist

include $(TOPDIR)/Rules.make

names.o: devlist.h

devlist.h: zorro.ids gen-devlist
	./gen-devlist <zorro.ids
