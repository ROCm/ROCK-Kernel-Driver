#
# Makefile for the ARC prom monitor library routines under Linux.
#

L_TARGET = arclib.a
L_OBJS  = init.o printf.o tree.o env.o cmdline.o misc.o time.o \
	  file.o identify.o

ifndef CONFIG_SGI_IP27
L_OBJS += console.o
endif

ifdef CONFIG_ARC_MEMORY
L_OBJS += memory.o
endif

include $(TOPDIR)/Rules.make
