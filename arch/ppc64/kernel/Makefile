#
# Makefile for the linux ppc64 kernel.
#

EXTRA_CFLAGS	+= -mno-minimal-toc
EXTRA_TARGETS	:= head.o
export-objs	:= ppc_ksyms.o

obj-y               :=	ppc_ksyms.o setup.o entry.o traps.o irq.o idle.o \
			time.o process.o signal.o syscalls.o misc.o ptrace.o \
			align.o semaphore.o bitops.o stab.o htab.o pacaData.o \
			LparData.o udbg.o binfmt_elf32.o sys_ppc32.o sys32.o \
			ioctl32.o ptrace32.o signal32.o open_pic.o xics.o \
			pmc.o mf_proc.o proc_pmc.o iSeries_setup.o \
			ItLpQueue.o hvCall.o mf.o HvLpEvent.o ras.o \
			iSeries_proc.o rtc.o init_task.o pSeries_htab.o \
			prom.o lmb.o rtas.o rtas-proc.o chrp_setup.o i8259.o \
			pci.o pci_dn.o pci_dma.o

obj-$(CONFIG_PPC_ISERIES) += iSeries_pci.o       iSeries_pci_reset.o \
			     iSeries_IoMmTable.o iSeries_irq.o       \
			     iSeries_VpdInfo.o   XmPciLpEvent.o \
			     HvCall.o HvLpConfig.o 

obj-$(CONFIG_PPC_PSERIES) += pSeries_pci.o pSeries_lpar.o pSeries_hvCall.o \
			     eeh.o rtasd.o nvram.o

obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_PROFILING)		+= profile.o
