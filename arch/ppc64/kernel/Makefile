#
# Makefile for the linux ppc64 kernel.
#

EXTRA_CFLAGS	+= -mno-minimal-toc
extra-y		:= head.o vmlinux.lds.s

obj-y               :=	setup.o entry.o traps.o irq.o idle.o \
			time.o process.o signal.o syscalls.o misc.o ptrace.o \
			align.o semaphore.o bitops.o stab.o htab.o pacaData.o \
			udbg.o binfmt_elf32.o sys_ppc32.o ioctl32.o \
			ptrace32.o signal32.o pmc.o rtc.o init_task.o \
			lmb.o pci.o pci_dn.o pci_dma.o cputable.o

obj-$(CONFIG_PPC_ISERIES) += iSeries_pci.o       iSeries_pci_reset.o \
			     iSeries_IoMmTable.o iSeries_irq.o \
			     iSeries_VpdInfo.o   XmPciLpEvent.o \
			     HvCall.o HvLpConfig.o LparData.o mf_proc.o \
			     iSeries_setup.o ItLpQueue.o hvCall.o \
			     mf.o HvLpEvent.o iSeries_proc.o 

obj-$(CONFIG_PPC_PSERIES) += pSeries_pci.o pSeries_lpar.o pSeries_hvCall.o \
			     eeh.o rtasd.o nvram.o ras.o

# Change this to pSeries only once we've got iSeries up to date
obj-y			  += open_pic.o xics.o pSeries_htab.o rtas.o \
			     chrp_setup.o i8259.o prom.o 

obj-$(CONFIG_PROC_FS)		+= proc_ppc64.o
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_MODULES)		+= module.o ppc_ksyms.o
obj-$(CONFIG_PPC_RTAS)		+= rtas-proc.o
obj-$(CONFIG_SCANLOG)		+= scanlog.o

CFLAGS_ioctl32.o += -Ifs/
