#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

L_TARGET := sgi.a

#
# Character and Audio devices for SGI machines.
#
subdir-y	+= char
subdir-m	+= char
obj-y		+= char/sgichar.o

include $(TOPDIR)/Rules.make
