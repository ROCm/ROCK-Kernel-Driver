#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := sgichar.o
OX_OBJS := newport.o
O_OBJS   := sgicons.o \
	usema.o shmiq.o streamable.o

ifeq ($(CONFIG_SGI_SERIAL),y)
  O_OBJS += sgiserial.o
endif

ifeq ($(CONFIG_SGI_DS1286),y)
  O_OBJS += ds1286.o
endif

ifeq ($(CONFIG_SGI_NEWPORT_GFX),y)
  O_OBJS +=  graphics.o rrm.o
else 
ifeq ($(CONFIG_SGI_NEWPORT_GFX),m)
  OX_OBJS  +=  graphics_syms.o
  MX_OBJS +=  graphics.o rrm.o
endif
endif

include $(TOPDIR)/Rules.make
