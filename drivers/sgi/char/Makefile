#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := sgichar.o

export-objs	:= newport.o shmiq.o sgicons.o usema.o
obj-y		:= newport.o shmiq.o sgicons.o usema.o streamable.o

obj-$(CONFIG_SGI_SERIAL)	+= sgiserial.o
obj-$(CONFIG_SGI_DS1286)	+= ds1286.o
obj-$(CONFIG_SGI_NEWPORT_GFX)	+= graphics.o rrm.o

include $(TOPDIR)/Rules.make
