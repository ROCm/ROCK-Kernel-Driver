# $Id: Makefile,v 1.1 1999/01/17 03:49:44 ralf Exp $
# Makefile for the DECstation prom monitor library routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

OBJS  = init.o memory.o cmdline.o identify.o locore.o

all: 	rexlib.a

rexlib.a: $(OBJS)
	$(AR) rcs rexlib.a $(OBJS)
	sync

locore.o: locore.S

dep:
	$(CPP) $(CPPFLAGS) -M *.c > .depend

include $(TOPDIR)/Rules.make
