#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS	 := extable.o init.o fault.o loadmmu.o

ifdef CONFIG_CPU_R3000
O_OBJS   += r2300.o
endif

ifdef CONFIG_CPU_R4300
O_OBJS   += r4xx0.o
endif

ifdef CONFIG_CPU_R4X00
O_OBJS   += r4xx0.o
endif

ifdef CONFIG_CPU_R5000
O_OBJS   += r4xx0.o
endif

ifdef CONFIG_CPU_NEVADA
O_OBJS   += r4xx0.o
endif

ifdef CONFIG_SGI_IP22
O_OBJS   += umap.o
endif

ifdef CONFIG_BAGET_MIPS
O_OBJS   += umap.o
endif

include $(TOPDIR)/Rules.make
