# $Id: Makefile,v 1.5 2001/05/15 05:10:00 hp Exp $
#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

all: kernel.o head.o

O_TARGET := kernel.o
obj-y   := process.o signal.o entry.o traps.o irq.o \
           ptrace.o setup.o time.o sys_cris.o shadows.o \
	   debugport.o semaphore.o

obj-$(CONFIG_ETRAX_KGDB) += kgdb.o

entry.o: entryoffsets.s

entryoffsets.s: entryoffsets.c
	$(CC) $(CFLAGS) -S -c $<

clean:

include $(TOPDIR)/Rules.make
