#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o

export-objs			+= ioremap.o umap.o
obj-y				+= extable.o init.o ioremap.o fault.o loadmmu.o

obj-$(CONFIG_CPU_R3000)		+= r2300.o
obj-$(CONFIG_CPU_R4300)		+= r4xx0.o
obj-$(CONFIG_CPU_R4X00)		+= r4xx0.o
obj-$(CONFIG_CPU_VR41XX)	+= r4xx0.o
obj-$(CONFIG_CPU_R5000)		+= r4xx0.o
obj-$(CONFIG_CPU_NEVADA)	+= r4xx0.o
obj-$(CONFIG_CPU_R5432)		+= r5432.o
obj-$(CONFIG_CPU_RM7000)	+= rm7k.o
obj-$(CONFIG_CPU_MIPS32)	+= mips32.o
obj-$(CONFIG_CPU_MIPS64)	+= mips32.o
obj-$(CONFIG_SGI_IP22)		+= umap.o
obj-$(CONFIG_BAGET_MIPS)	+= umap.o

include $(TOPDIR)/Rules.make
