#
# Makefile for the ARC prom monitor library routines under Linux.
#

lib-y	:= console.o init.o identify.o tree.o env.o cmdline.o misc.o time.o \
	   file.o

lib-$(CONFIG_ARC_MEMORY) += memory.o
lib-$(CONFIG_ARC_CONSOLE) += arc_con.o
