#
# Makefile for the Jazz family specific parts of the kernel
#

O_TARGET := jazz.o

obj-y	 := int-handler.o irq.o jazzdma.o reset.o rtc-jazz.o setup.o \
	    floppy-jazz.o kbd-jazz.o

EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make
