#
# Makefile for the Jazz family specific parts of the kernel
#

export-syms	:= jazz-ksyms.o
obj-y	 	:= int-handler.o irq.o jazzdma.o jazz-ksyms.o reset.o \
		   rtc-jazz.o setup.o floppy-jazz.o kbd-jazz.o

EXTRA_AFLAGS := $(CFLAGS)
