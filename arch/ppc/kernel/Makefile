#
# Makefile for the linux kernel.
#

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif
ifdef CONFIG_4xx
EXTRA_AFLAGS		:= -Wa,-m405
endif

# Start off with 'head.o', change as needed.
HEAD-y				:= head.o
HEAD-$(CONFIG_40x)		:= head_4xx.o
HEAD-$(CONFIG_8xx)		:= head_8xx.o
HEAD-$(CONFIG_PPC_ISERIES)	:= iSeries_head.o

EXTRA_TARGETS	:= $(HEAD-y)

export-objs			:= ppc_ksyms.o time.o

obj-y				:= entry.o traps.o irq.o idle.o time.o misc.o \
					process.o signal.o ptrace.o align.o \
					semaphore.o syscalls.o setup.o \
					cputable.o ppc_htab.o
obj-$(CONFIG_6xx)		+= l2cr.o
obj-$(CONFIG_MODULES)		+= ppc_ksyms.o
obj-$(CONFIG_PCI)		+= pci.o 
ifneq ($(CONFIG_PPC_ISERIES),y)
obj-$(CONFIG_PCI)		+= pci-dma.o
endif
obj-$(CONFIG_PPCBUG_NVRAM)	+= prep_nvram.o
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_TAU)		+= temp.o
ifeq ($(CONFIG_8xx),y)
ifneq ($(CONFIG_MATH_EMULATION),n)
obj-y				+= softemu8xx.o
endif
endif
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_misc.o

include $(TOPDIR)/Rules.make

find_name : find_name.c
	$(HOSTCC) $(HOSTCFLAGS) -o find_name find_name.c
