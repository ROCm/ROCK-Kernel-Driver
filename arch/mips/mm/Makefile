#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				+= cache.o extable.o fault.o init.o pgtable.o

obj-$(CONFIG_MIPS32)		+= ioremap.o pgtable-32.o
obj-$(CONFIG_MIPS64)		+= pgtable-64.o
obj-$(CONFIG_HIGHMEM)		+= highmem.o

obj-$(CONFIG_CPU_MIPS32)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_R10000)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-andes.o
obj-$(CONFIG_CPU_R3000)		+= c-r3k.o tlb-r3k.o pg-r4k.o
obj-$(CONFIG_CPU_R4300)		+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_R5000)		+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_R5432)		+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_R8000)		+= c-r4k.o cex-gen.o pg-r4k.o tlb-r8k.o
obj-$(CONFIG_CPU_RM7000)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_RM9000)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_SB1)		+= c-sb1.o cerr-sb1.o cex-sb1.o pg-sb1.o \
				   tlb-sb1.o
obj-$(CONFIG_CPU_TX39XX)	+= c-tx39.o pg-r4k.o tlb-r3k.o
obj-$(CONFIG_CPU_TX49XX)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o
obj-$(CONFIG_CPU_VR41XX)	+= c-r4k.o cex-gen.o pg-r4k.o tlb-r4k.o

#
# TLB exception handling code differs between 32-bit and 64-bit kernels.
#
ifdef CONFIG_MIPS32
obj-$(CONFIG_CPU_R3000)		+= tlbex32-r3k.o
obj-$(CONFIG_CPU_TX49XX)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_R4300)		+= tlbex32-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= tlbex32-r4k.o
obj-$(CONFIG_CPU_VR41XX)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_R5000)		+= tlbex32-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_R5432)		+= tlbex32-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_RM9000)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_R10000)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_MIPS32)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= tlbex32-r4k.o
obj-$(CONFIG_CPU_SB1)		+= tlbex32-r4k.o
obj-$(CONFIG_CPU_TX39XX)	+= tlbex32-r3k.o
endif
ifdef CONFIG_MIPS64
obj-$(CONFIG_CPU_R4300)		+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_R5000)		+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_R5432)		+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_RM9000)	+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_R10000)	+= tlb64-glue-r4k.o tlbex64-r4k.o
obj-$(CONFIG_CPU_SB1)		+= tlb64-glue-sb1.o tlbex64-r4k.o
obj-$(CONFIG_CPU_MIPS64)	+= tlb64-glue-r4k.o tlbex64-r4k.o
endif


obj-$(CONFIG_IP22_CPU_SCACHE)	+= sc-ip22.o
obj-$(CONFIG_R5000_CPU_SCACHE)  += sc-r5k.o
obj-$(CONFIG_RM7000_CPU_SCACHE)	+= sc-rm7k.o

#
# Choose one DMA coherency model
#
obj-$(CONFIG_DMA_COHERENT)	+= dma-coherent.o
obj-$(CONFIG_DMA_NONCOHERENT)	+= dma-noncoherent.o
obj-$(CONFIG_DMA_IP27)		+= dma-ip27.o

EXTRA_AFLAGS := $(CFLAGS)
