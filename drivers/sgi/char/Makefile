#
# Makefile for the linux kernel.
#

export-objs	:= newport.o shmiq.o sgicons.o usema.o
obj-y		:= newport.o shmiq.o sgicons.o usema.o streamable.o

obj-$(CONFIG_SGI_SERIAL)	+= sgiserial.o
obj-$(CONFIG_SGI_DS1286)	+= ds1286.o
obj-$(CONFIG_SGI_NEWPORT_GFX)	+= graphics.o rrm.o

include $(TOPDIR)/Rules.make
