# $Id: Makefile,v 1.1.1.1 2001/12/17 13:59:27 bjornw Exp $
#
# Makefile for the linux kernel.
#

O_TARGET	:= kernel.o
EXTRA_TARGETS	:= head.o

export-objs := ksyms.o

obj-y   := process.o signal.o entry.o traps.o irq.o \
           ptrace.o setup.o time.o sys_cris.o shadows.o \
	   debugport.o semaphore.o

obj-$(CONFIG_MODULES)    += ksyms.o
obj-$(CONFIG_ETRAX_KGDB) += kgdb.o

# This dependency isn't caught by mkdep.  See entry.S.
entry.o: entryoffsets.s

entryoffsets.s: entryoffsets.c
	$(CC) $(CFLAGS) -S -c $<

clean:

include $(TOPDIR)/Rules.make
