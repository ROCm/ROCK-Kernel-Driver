#
# Makefile for the linux kernel.
#

EXTRA_TARGETS	:= head.o init_task.o process.o pdc_cons.o

# Object file lists.

obj-y	     	:= cache.o setup.o traps.o time.o irq.o \
		   syscall.o entry.o sys_parisc.o pdc.o ptrace.o hardware.o \
		   inventory.o drivers.o semaphore.o pa7300lc.o pci-dma.o \
		   signal.o hpmc.o \
		   real1.o real2.o led.o parisc_ksyms.o

export-objs	:= parisc_ksyms.o

obj-$(CONFIG_PCI) += pci.o
obj-$(CONFIG_VT) += keyboard.o
obj-$(CONFIG_PCI_LBA) += lba_pci.o
# I/O SAPIC is also on IA64 platforms.
# The two could be merged into a common source some day.
obj-$(CONFIG_IOSAPIC) += iosapic.o
obj-$(CONFIG_IOMMU_SBA) += sba_iommu.o
# Only use one of them: ccio-rm-dma is for PCX-W systems *only*
# obj-$(CONFIG_IOMMU_CCIO) += ccio-rm-dma.o
obj-$(CONFIG_IOMMU_CCIO) += ccio-dma.o

EXTRA_AFLAGS	:= -traditional

include $(TOPDIR)/Rules.make
