#
# Makefile for the linux arm-specific parts of the memory manager.
#

# Object file lists.

obj-y		:= consistent.o extable.o fault-armv.o fault-common.o \
		   init.o ioremap.o mm-armv.o
obj-$(CONFIG_MODULES) += proc-syms.o

obj-$(CONFIG_ALIGNMENT_TRAP) += alignment.o
obj-$(CONFIG_DISCONTIGMEM) += discontig.o

# ARMv3
p-$(CONFIG_CPU_ARM610)	+= proc-arm6_7.o  tlb-v3.o    cache-v3.o    copypage-v3.o
p-$(CONFIG_CPU_ARM710)	+= proc-arm6_7.o  tlb-v3.o    cache-v3.o    copypage-v3.o

# ARMv4
p-$(CONFIG_CPU_ARM720T)	+= proc-arm720.o  tlb-v4.o    cache-v4.o    copypage-v4wt.o abort-lv4t.o
p-$(CONFIG_CPU_ARM920T)	+= proc-arm920.o  tlb-v4wbi.o cache-v4wt.o  copypage-v4wb.o abort-ev4t.o
p-$(CONFIG_CPU_ARM922T)	+= proc-arm922.o  tlb-v4wbi.o cache-v4wt.o  copypage-v4wb.o abort-ev4t.o
p-$(CONFIG_CPU_ARM1020)	+= proc-arm1020.o tlb-v4wbi.o cache-v4wt.o  copypage-v4wb.o abort-ev4t.o
p-$(CONFIG_CPU_SA110)	+= proc-sa110.o   tlb-v4wb.o  cache-v4wb.o  copypage-v4wb.o abort-ev4.o
p-$(CONFIG_CPU_SA1100)	+= proc-sa1100.o  tlb-v4wb.o  cache-v4wb.o  copypage-v4mc.o abort-ev4.o   minicache.o

# ARMv5
p-$(CONFIG_CPU_ARM926T)	+= proc-arm926.o  tlb-v4wbi.o copypage-v4wb.o abort-ev5tej.o
p-$(CONFIG_CPU_XSCALE)	+= proc-xscale.o  tlb-v4wbi.o copypage-xscale.o abort-xscale.o minicache.o

obj-y		+= $(sort $(p-y))
