#
# Makefile for the DMA sound driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET = _dmasound.o

export-objs := dmasound_core.o

obj-$(CONFIG_DMASOUND_ATARI)  += dmasound_core.o dmasound_atari.o
obj-$(CONFIG_DMASOUND_AWACS)  += dmasound_core.o dmasound_awacs.o
obj-$(CONFIG_DMASOUND_PAULA)  += dmasound_core.o dmasound_paula.o
obj-$(CONFIG_DMASOUND_Q40)    += dmasound_core.o dmasound_q40.o

include $(TOPDIR)/Rules.make
