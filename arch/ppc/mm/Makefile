#
# Makefile for the linux ppc-specific parts of the memory manager.
#

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif

obj-y				:= fault.o init.o mem_pieces.o extable.o \
					mmu_context.o pgtable.o

obj-$(CONFIG_PPC_STD_MMU)	+= hashtable.o ppc_mmu.o tlb.o
obj-$(CONFIG_40x)		+= 4xx_mmu.o
obj-$(CONFIG_44x)		+= 44x_mmu.o
obj-$(CONFIG_NOT_COHERENT_CACHE)	+= cachemap.o
