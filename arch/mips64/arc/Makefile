#
# Makefile for the ARC prom monitor library routines under Linux.
#

L_TARGET = arclib.a
obj-y	:= console.o init.o identify.o tree.o env.o cmdline.o misc.o time.o \
	   file.o

obj-$(CONFIG_ARC_MEMORY) += memory.o
obj-$(CONFIG_ARC_CONSOLE) += arc_con.o

include $(TOPDIR)/Rules.make
