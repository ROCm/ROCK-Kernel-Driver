#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= cache.o extable.o init.o ioremap.o fault.o \
				   pgtable.o loadmmu.o

obj-$(CONFIG_HIGHMEM)		+= highmem.o

obj-$(CONFIG_CPU_R3000)		+= pg-r3k.o c-r3k.o tlb-r3k.o tlbex-r3k.o
obj-$(CONFIG_CPU_TX39XX)	+= pg-r3k.o c-tx39.o tlb-r3k.o tlbex-r3k.o
obj-$(CONFIG_CPU_TX49XX)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R4300)		+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_VR41XX)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R5000)		+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R5432)		+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_R10000)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_MIPS32)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= pg-r4k.o c-r4k.o tlb-r4k.o tlbex-r4k.o
obj-$(CONFIG_CPU_SB1)		+= c-sb1.o cex-sb1.o cerr-sb1.o pg-sb1.o \
				   tlb-sb1.o tlbex-r4k.o

obj-$(CONFIG_CPU_RM7000)	+= sc-rm7k.o
obj-$(CONFIG_R5000_CPU_SCACHE)  += sc-r5k.o
obj-$(CONFIG_SGI_IP22)		+= sc-ip22.o

EXTRA_AFLAGS := $(CFLAGS)
