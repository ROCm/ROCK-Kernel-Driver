#
# Makefile for the Jazz family specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $@
.S.o:
	$(CC) $(CFLAGS) -c $< -o $@

all: jazz.o

O_TARGET := jazz.o

obj-y	 := int-handler.o irq.o jazzdma.o reset.o rtc-jazz.o setup.o \
	    floppy-jazz.o kbd-jazz.o

int-handler.o: int-handler.S

include $(TOPDIR)/Rules.make
