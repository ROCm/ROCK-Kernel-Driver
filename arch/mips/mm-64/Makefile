#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#

obj-y				:= tlbex-r4k.o

obj-$(CONFIG_CPU_R4300)		+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_R4X00)		+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5000)		+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_NEVADA)	+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_R5432)		+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_RM7000)	+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_RM9000)	+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_R10000)	+= tlb-glue-r4k.o
obj-$(CONFIG_CPU_SB1)		+= tlb-glue-sb1.o
obj-$(CONFIG_CPU_MIPS64)	+= tlb-glue-r4k.o

#
# Debug TLB exception handler, currently unused
#
#obj-y				+=  tlb-dbg-r4k.o

AFLAGS_tlb-glue-r4k.o := -P

EXTRA_AFLAGS := $(CFLAGS)
