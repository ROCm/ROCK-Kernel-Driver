#
# Makefile for the SGI arcs prom monitor library routines
# under Linux.
#

L_TARGET = lib.a

obj-y		+= console.o init.o memory.o tree.o env.o cmdline.o misc.o \
		   time.o file.o identify.o

obj-$(CONFIG_ARC_CONSOLE)   += arc_con.o

include $(TOPDIR)/Rules.make
