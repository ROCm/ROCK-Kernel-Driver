#
# Makefile for the linux ppc64 kernel.
#

EXTRA_CFLAGS	+= -mno-minimal-toc
extra-y		:= head.o vmlinux.lds.s

obj-y               :=	setup.o entry.o traps.o irq.o idle.o dma.o \
			time.o process.o signal.o syscalls.o misc.o ptrace.o \
			align.o semaphore.o bitops.o stab.o pacaData.o \
			udbg.o binfmt_elf32.o sys_ppc32.o ioctl32.o \
			ptrace32.o signal32.o rtc.o init_task.o \
			lmb.o cputable.o cpu_setup_power4.o idle_power4.o \
			iommu.o sysfs.o vio.o

obj-$(CONFIG_PPC_OF) +=	of_device.o

obj-$(CONFIG_PCI)	+= pci.o pci_dn.o pci_iommu.o

ifdef CONFIG_PPC_ISERIES
obj-$(CONFIG_PCI)	+= iSeries_pci.o iSeries_pci_reset.o \
			     iSeries_IoMmTable.o 
else
obj-$(CONFIG_PCI)	+= pci_dma_direct.o
endif

obj-$(CONFIG_PPC_ISERIES) += iSeries_irq.o \
			     iSeries_VpdInfo.o XmPciLpEvent.o \
			     HvCall.o HvLpConfig.o LparData.o mf_proc.o \
			     iSeries_setup.o ItLpQueue.o hvCall.o \
			     mf.o HvLpEvent.o iSeries_proc.o iSeries_htab.o \
			     iSeries_iommu.o

obj-$(CONFIG_PPC_PSERIES) += pSeries_pci.o pSeries_lpar.o pSeries_hvCall.o \
			     eeh.o nvram.o pSeries_nvram.o rtasd.o ras.o \
			     open_pic.o xics.o pSeries_htab.o rtas.o \
			     chrp_setup.o i8259.o prom.o pSeries_iommu.o

obj-$(CONFIG_PROC_FS)		+= proc_ppc64.o
obj-$(CONFIG_RTAS_FLASH)	+= rtas_flash.o
obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_MODULES)		+= module.o ppc_ksyms.o
obj-$(CONFIG_PPC_RTAS)		+= rtas-proc.o
obj-$(CONFIG_SCANLOG)		+= scanlog.o
obj-$(CONFIG_VIOPATH)		+= viopath.o
obj-$(CONFIG_LPARCFG)		+= lparcfg.o
obj-$(CONFIG_HVC_CONSOLE)	+= hvconsole.o
obj-$(CONFIG_BOOTX_TEXT)	+= btext.o

obj-$(CONFIG_PPC_PMAC)		+= pmac_setup.o pmac_feature.o pmac_pci.o \
				   pmac_time.o pmac_nvram.o pmac_low_i2c.o \
				   open_pic_u3.o
obj-$(CONFIG_PMAC_DART)		+= pmac_iommu.o

ifdef CONFIG_SMP
obj-$(CONFIG_PPC_PMAC)		+= pmac_smp.o smp-tbsync.o
endif

CFLAGS_ioctl32.o += -Ifs/
