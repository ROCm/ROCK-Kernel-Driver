# BK Id: SCCS/s.Makefile 1.6 06/28/01 15:50:17 paulus
#
#
# Makefile for the linux ppc-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

USE_STANDARD_AS_RULE	:= true

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif

O_TARGET		:= mm.o
obj-y			:= fault.o init.o mem_pieces.o extable.o

ifneq ($(CONFIG_8xx),y)
ifneq ($(CONFIG_4xx),y)
obj-y			+= hashtable.o
endif
endif

obj-$(CONFIG_4xx)	+= 4xx_tlb.o

include $(TOPDIR)/Rules.make
