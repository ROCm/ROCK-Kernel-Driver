# $Id: Makefile,v 1.1 1999/01/17 03:49:44 ralf Exp $
# Makefile for the DECstation prom monitor library routines
# under Linux.
#

lib-y := init.o memory.o cmdline.o identify.o locore.o

EXTRA_AFLAGS := $(CFLAGS)

dep:
	$(CPP) $(CPPFLAGS) -M *.c > .depend
