# $Id: Makefile,v 1.8 2000/12/14 22:57:25 davem Exp $
# Makefile for the linux Sparc64-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

.S.s:
	$(CPP) $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -c $< -o $*.o

O_TARGET := mm.o
obj-y    := ultra.o fault.o init.o generic.o extable.o modutil.o

include $(TOPDIR)/Rules.make
