# $Id: Makefile,v 1.7 2000/12/14 22:57:25 davem Exp $
# Makefile for the Sun Boot PROM interface library under
# Linux.
#

EXTRA_AFLAGS := -ansi

L_TARGET = lib.a
obj-y   := bootstr.o devops.o init.o memory.o misc.o \
	   tree.o console.o printf.o p1275.o map.o

include $(TOPDIR)/Rules.make

%.o: %.c
	$(CC) $(subst -pg,,$(CFLAGS)) -c $<
