#
# Makefile for the NEC DDB Vrc-5074 specific kernel interface routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...
#

EXTRA_AFLAGS := $(CFLAGS)

O_TARGET = ddb5074.a

obj-y	:= setup.o irq.o time.o prom.o pci.o int-handler.o nile4.o

include $(TOPDIR)/Rules.make
