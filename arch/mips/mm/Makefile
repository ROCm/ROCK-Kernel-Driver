#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= cache.o extable.o fault.o loadmmu.o pgtable.o

obj-$(CONFIG_MIPS32)		+= ioremap.o pgtable-32.o
obj-$(CONFIG_MIPS64)		+= pgtable-64.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o

obj-$(CONFIG_CPU_MIPS32)	+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_R10000)	+= c-r4k.o cex-gen.o tlb-andes.o
obj-$(CONFIG_CPU_R3000)		+= pg-r3k.o c-r3k.o tlb-r3k.o tlbex-r3k.o
obj-$(CONFIG_CPU_R4300)		+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_R5000)		+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_R5432)		+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_SB1)		+= c-sb1.o cerr-sb1.o cex-sb1.o pg-sb1.o \
				   tlb-sb1.o
obj-$(CONFIG_CPU_TX39XX)	+= c-tx39.o pg-r3k.o tlb-r3k.o tlbex-r3k.o
obj-$(CONFIG_CPU_TX49XX)	+= c-r4k.o cex-gen.o tlb-r4k.o
obj-$(CONFIG_CPU_VR41XX)	+= c-r4k.o cex-gen.o tlb-r4k.o

obj-$(CONFIG_CPU_RM7000)	+= sc-rm7k.o
obj-$(CONFIG_R5000_CPU_SCACHE)  += sc-r5k.o
obj-$(CONFIG_SGI_IP22)		+= sc-ip22.o

EXTRA_AFLAGS := $(CFLAGS)
