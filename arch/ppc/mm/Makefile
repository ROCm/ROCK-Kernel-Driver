# BK Id: SCCS/s.Makefile 1.8 08/16/01 17:25:47 paulus
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

O_TARGET			:= mm.o
obj-y				:= fault.o init.o mem_pieces.o extable.o \
					mmu_context.o pgtable.o

obj-$(CONFIG_PPC_STD_MMU)	+= hashtable.o ppc_mmu.o tlb.o
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_hashtable.o iSeries_mmu.o tlb.o
obj-$(CONFIG_4xx)		+= cachemap.o 4xx_mmu.o
obj-$(CONFIG_8xx)		+= cachemap.o

include $(TOPDIR)/Rules.make
