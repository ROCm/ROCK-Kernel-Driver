# $Id: Makefile,v 1.8 2000/12/15 00:41:22 davem Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#

L_TARGET = lib.a

obj-y := bootstr.o devmap.o devops.o init.o memory.o misc.o mp.o \
	 palloc.o ranges.o segment.o console.o printf.o tree.o

obj-$(CONFIG_SUN4) += sun4prom.o

include $(TOPDIR)/Rules.make
