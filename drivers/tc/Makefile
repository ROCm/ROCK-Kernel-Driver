#
# Makefile for the linux kernel.
#

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs	:= tc.o

# Object file lists.

obj-$(CONFIG_TC) += tc.o
obj-$(CONFIG_ZS) += zs.o
obj-$(CONFIG_VT) += lk201.o lk201-map.o lk201-remap.o

include $(TOPDIR)/Rules.make

lk201-map.c: lk201-map.map
	loadkeys --mktable lk201-map.map > lk201-map.c
