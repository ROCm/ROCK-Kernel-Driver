#
# Makefile for the Zorro bus specific drivers.
#

export-objs		:= zorro.o

obj-$(CONFIG_ZORRO)	+= zorro.o names.o
obj-$(CONFIG_PROC_FS)	+= proc.o

include $(TOPDIR)/Rules.make

names.o: names.c devlist.h

devlist.h: zorro.ids gen-devlist
	./gen-devlist <zorro.ids

gen-devlist: gen-devlist.c
	$(HOSTCC) $(HOSTCFLAGS) -o gen-devlist gen-devlist.c
