#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				:= init.o tlbex-r4k.o

obj-$(CONFIG_CPU_R4300)		+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5000)		+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5432)		+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_R10000)	+= pg-r4k.o tlb-glue-r4k.o
obj-$(CONFIG_CPU_SB1)		+= tlb-glue-sb1.o
obj-$(CONFIG_CPU_MIPS64)	+= pg-r4k.o tlb-glue-r4k.o

#
# Debug TLB exception handler, currently unused
#
#obj-y				+=  tlb-dbg-r4k.o

AFLAGS_tlb-glue-r4k.o := -P

EXTRA_AFLAGS := $(CFLAGS)
