#
# Makefile for the linux kernel.
#

EXTRA_CFLAGS = -mno-minimal-toc

KHEAD := head.o

O_TARGET	:= kernel.o
EXTRA_TARGETS	:= $(KHEAD)

export-objs         := ppc_ksyms.o

obj-y               :=	ppc_ksyms.o setup.o entry.o traps.o irq.o idle.o \
			time.o process.o signal.o syscalls.o misc.o ptrace.o \
			align.o semaphore.o bitops.o stab.o htab.o pacaData.o \
			LparData.o udbg.o binfmt_elf32.o sys_ppc32.o sys32.o \
			ioctl32.o ptrace32.o signal32.o open_pic.o xics.o \
			pmc.o mf_proc.o proc_pmc.o iSeries_setup.o \
			ItLpQueue.o hvCall.o mf.o HvLpEvent.o ras.o \
			iSeries_proc.o HvCall.o HvLpConfig.o \
			rtc.o init_task.o pSeries_htab.o

obj-$(CONFIG_PCI) +=  pci.o pci_dn.o pci_dma.o

ifeq ($(CONFIG_PPC_ISERIES),y)
obj-$(CONFIG_PCI) += iSeries_pci.o iSeries_pci_reset.o iSeries_IoMmTable.o iSeries_irq.o iSeries_VpdInfo.o XmPciLpEvent.o 
endif
ifeq ($(CONFIG_PPC_PSERIES),y)
obj-$(CONFIG_PCI) += pSeries_pci.o pSeries_lpar.o pSeries_hvCall.o eeh.o

obj-y += rtasd.o nvram.o
endif

obj-$(CONFIG_RTAS_FLASH) += rtas_flash.o

obj-$(CONFIG_SMP) += smp.o

obj-y += prom.o lmb.o rtas.o rtas-proc.o chrp_setup.o i8259.o

include $(TOPDIR)/Rules.make

