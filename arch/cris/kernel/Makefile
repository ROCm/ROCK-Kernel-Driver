# $Id: Makefile,v 1.3 2001/01/10 21:11:07 bjornw Exp $
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

obj-$(CONFIG_KGDB) += kgdb.o

clean:

include $(TOPDIR)/Rules.make
