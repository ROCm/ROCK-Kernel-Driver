#
# Makefile for the ARC prom monitor library routines under Linux.
#

L_TARGET = arclib.a
obj-y	:= init.o printf.o tree.o env.o cmdline.o misc.o time.o file.o \
	   identify.o

ifndef CONFIG_SGI_IP27
  obj-y += console.o
endif

obj-$(CONFIG_ARC_MEMORY) += memory.o

include $(TOPDIR)/Rules.make
