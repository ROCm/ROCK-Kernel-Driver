#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

EXTRA_CFLAGS	+= -I../kernel
obj-y			:= setup.o voyager_basic.o voyager_thread.o

obj-$(CONFIG_SMP)	+= voyager_smp.o voyager_cat.o
