#
# Makefile for the Linux IRDA compression protocols
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET :=

obj-y := 
obj-m := 

obj-$(CONFIG_IRDA_DEFLATE) += irda_deflate.o

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
