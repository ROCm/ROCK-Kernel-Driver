#
# Makefile for the linux alpha-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

OBJS	= init.o fault.o extable.o

mm.o: $(OBJS)
	$(LD) -r -o mm.o $(OBJS)

include $(TOPDIR)/Rules.make
