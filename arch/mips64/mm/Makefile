# $Id: Makefile,v 1.4 2000/01/17 23:32:46 ralf Exp $
#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

O_TARGET := mm.o
O_OBJS	 := extable.o init.o fault.o loadmmu.o

ifdef CONFIG_CPU_R4300
O_OBJS	+= r4xx0.o
endif
ifdef CONFIG_CPU_R4X00
O_OBJS	+= r4xx0.o
endif
ifdef CONFIG_CPU_R5000
O_OBJS	+= r4xx0.o
endif
ifdef CONFIG_CPU_NEVADA
O_OBJS	+= r4xx0.o
endif
ifdef CONFIG_CPU_R10000
O_OBJS	+= andes.o
endif

ifdef CONFIG_SGI_IP22
O_OBJS   += umap.o
endif

ifdef CONFIG_BAGET_MIPS
O_OBJS   += umap.o
endif

include $(TOPDIR)/Rules.make
