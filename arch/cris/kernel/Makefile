# $Id: Makefile,v 1.8 2003/04/09 05:20:47 starvik Exp $
#
# Makefile for the linux kernel.
#

extra-y := vmlinux.lds.s

obj-y   := process.o traps.o irq.o ptrace.o setup.o \
	   time.o sys_cris.o semaphore.o

obj-$(CONFIG_MODULES)    += ksyms.o
obj-$(CONFIG_MODULES)	 += module.o

clean:

