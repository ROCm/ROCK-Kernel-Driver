#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

O_TARGET := mm.o

export-objs			+= umap.o
obj-y				:= extable.o init.o fault.o loadmmu.o

obj-$(CONFIG_CPU_R4300)		+= r4xx0.o
obj-$(CONFIG_CPU_R4X00)		+= r4xx0.o
obj-$(CONFIG_CPU_R5000)		+= r4xx0.o
obj-$(CONFIG_CPU_NEVADA)	+= r4xx0.o
obj-$(CONFIG_CPU_R10000)	+= andes.o
obj-$(CONFIG_SGI_IP22)		+= umap.o

include $(TOPDIR)/Rules.make
