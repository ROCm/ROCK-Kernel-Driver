#
# Makefile for the Acorn block device drivers.
#

USE_STANDARD_AS_RULE := true

O_TARGET	:= acorn-block.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

export-objs	:=
fd1772_mod-objs	:= fd1772.o fd1772dma.o
mfmhd_mod-objs	:= mfmhd.o mfm.o

obj-$(CONFIG_BLK_DEV_FD1772)	+= fd1772_mod.o
obj-$(CONFIG_BLK_DEV_MFM)	+= mfmhd_mod.o

include $(TOPDIR)/Rules.make
