#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(AFLAGS) -traditional -c $< -o $*.o

all: mach-generic.o

O_TARGET 	:= mach-generic.o
EXTRA_CFLAGS	+= -I../kernel
export-objs     := 

obj-y				:= setup.o

include $(TOPDIR)/Rules.make
