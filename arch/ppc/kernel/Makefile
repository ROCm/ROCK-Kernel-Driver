# BK Id: %F% %I% %G% %U% %#%
#
#
# Makefile for the linux kernel.
#

ifdef CONFIG_PPC64BRIDGE
EXTRA_AFLAGS		:= -Wa,-mppc64bridge
endif
ifdef CONFIG_4xx
EXTRA_AFLAGS		:= -Wa,-m405
endif

CFLAGS_prom_init.o	+= -mrelocatable-lib
CFLAGS_btext.o		+= -mrelocatable-lib

# Start off with 'head.o', change as needed.
HEAD-y				:= head.o
HEAD-$(CONFIG_40x)		:= head_4xx.o
HEAD-$(CONFIG_8xx)		:= head_8xx.o
HEAD-$(CONFIG_PPC_ISERIES)	:= iSeries_head.o

O_TARGET 	:= kernel.o
EXTRA_TARGETS	:= $(HEAD-y)

export-objs			:= ppc_ksyms.o time.o ppc405_dma.o

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
obj-$(CONFIG_KGDB)		+= ppc-stub.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_TAU)		+= temp.o
ifeq ($(CONFIG_4xx),y)
obj-$(CONFIG_4xx)		+= ppc4xx_setup.o ppc4xx_pic.o ppc4xx_serial.o
obj-$(CONFIG_PPC_RTC)		+= todc_time.o
obj-$(CONFIG_KGDB)		+= ppc4xx_kgdb.o
obj-$(CONFIG_405_DMA)		+= ppc405_dma.o
obj-$(CONFIG_PCI)		+= ppc405_pci.o indirect_pci.o pci_auto.o
obj-$(CONFIG_PM)		+= ppc4xx_pm.o
endif
obj-$(CONFIG_8xx)		+= m8xx_setup.o ppc8xx_pic.o
ifeq ($(CONFIG_8xx),y)
obj-$(CONFIG_PCI)		+= qspan_pci.o i8259.o
ifneq ($(CONFIG_MATH_EMULATION),n)
obj-y				+= softemu8xx.o
endif
endif
obj-$(CONFIG_ALL_PPC)		+= prom_init.o prom.o open_pic.o \
					indirect_pci.o i8259.o
obj-$(CONFIG_ADIR)		+= i8259.o indirect_pci.o pci_auto.o \
					todc_time.o
obj-$(CONFIG_EV64260)		+= gt64260_common.o gt64260_pic.o \
					indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_GEMINI)		+= open_pic.o i8259.o
obj-$(CONFIG_K2)		+= i8259.o indirect_pci.o todc_time.o \
					pci_auto.o
obj-$(CONFIG_LOPEC)		+= mpc10x_common.o indirect_pci.o pci_auto.o \
					open_pic.o i8259.o todc_time.o
obj-$(CONFIG_MCPN765)		+= todc_time.o indirect_pci.o pci_auto.o \
					open_pic.o i8259.o pplus_common.o
obj-$(CONFIG_MENF1)		+= todc_time.o i8259.o mpc10x_common.o \
					pci_auto.o indirect_pci.o
obj-$(CONFIG_MVME5100)		+= open_pic.o todc_time.o indirect_pci.o \
					i8259.o pci_auto.o pplus_common.o
obj-$(CONFIG_PCORE)		+= mpc10x_common.o todc_time.o i8259.o \
					indirect_pci.o pci_auto.o
obj-$(CONFIG_POWERPMC250)	+= open_pic.o mpc10x_common.o \
					indirect_pci.o pci_auto.o
obj-$(CONFIG_PPLUS)		+= pplus_common.o open_pic.o i8259.o \
				   indirect_pci.o todc_time.o pci_auto.o
obj-$(CONFIG_PRPMC750)		+= open_pic.o indirect_pci.o pci_auto.o \
					pplus_common.o
obj-$(CONFIG_PRPMC800)		+= open_pic.o indirect_pci.o pci_auto.o \
					pplus_common.o harrier.o
obj-$(CONFIG_SANDPOINT)		+= i8259.o open_pic.o mpc10x_common.o \
					pci_auto.o indirect_pci.o todc_time.o
obj-$(CONFIG_SPRUCE)		+= indirect_pci.o pci_auto.o todc_time.o
obj-$(CONFIG_ZX4500)		+= indirect_pci.o pci_auto.o mpc10x_common.o \
					i8259.o open_pic.o
obj-$(CONFIG_8260)		+= m8260_setup.o ppc8260_pic.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o
obj-$(CONFIG_PPC_ISERIES)	+= iSeries_misc.o

include $(TOPDIR)/Rules.make

entry.o: entry.S asm-offsets.h
misc.o: misc.S asm-offsets.h
l2cr.o: l2cr.S asm-offsets.h
head.o: head.S asm-offsets.h
head_4xx.o: head_4xx.S asm-offsets.h
head_8xx.o: head_8xx.S asm-offsets.h
iSeries_head.o: iSeries_head.S asm-offsets.h
iSeries_misc.o: iSeries_misc.S asm-offsets.h

asm-offsets.h: mk_defs.c asm-offsets.head \
		$(TOPDIR)/include/asm/mmu.h \
		$(TOPDIR)/include/asm/processor.h \
		$(TOPDIR)/include/asm/pgtable.h \
		$(TOPDIR)/include/asm/ptrace.h
	$(CC) $(CFLAGS) -S mk_defs.c
	(cat asm-offsets.head; grep '^#define' mk_defs.s) >asm-offsets.h
	rm mk_defs.s

find_name : find_name.c
	$(HOSTCC) $(HOSTCFLAGS) -o find_name find_name.c
