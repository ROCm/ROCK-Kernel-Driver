#
# Makefile for the SGI arcs prom monitor library routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

L_TARGET = arclib.a

obj-y		+= console.o init.o memory.o tree.o env.o cmdline.o misc.o \
		   time.o file.o identify.o

obj-$(CONFIG_ARC_CONSOLE)   += arc_con.o

include $(TOPDIR)/Rules.make
