#
# Makefile for the linux ppc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET		:= mm.o
obj-y			:= fault.o init.o mem_pieces.o extable.o

obj-$(CONFIG_4xx)	+= 4xx_tlb.o

include $(TOPDIR)/Rules.make
