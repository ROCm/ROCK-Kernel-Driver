#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

USE_STANDARD_AS_RULE := true

O_TARGET		:= sa1100.o

# Object file lists.

obj-y			:= arch.o hw.o dma-sa1100.o # mm.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= dma-sa1100.o dma-sa1111.o hw.o leds.o

obj-$(CONFIG_LEDS)	+= leds.o
obj-$(CONFIG_SA1111)	+= dma-sa1111.o

include $(TOPDIR)/Rules.make
