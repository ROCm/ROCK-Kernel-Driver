#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

all: kernel.o init_task.o pdc_cons.o process.o head.o
O_TARGET = kernel.o
O_OBJS = 

# Object file lists.

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

obj-y		+= cache.o setup.o traps.o time.o irq.o \
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

.o.S:	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $*.S -o $*.o

# Translate to Rules.make lists.

O_OBJS          := $(filter-out $(export-objs), $(obj-y))
OX_OBJS         := $(filter     $(export-objs), $(obj-y))
M_OBJS          := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS         := $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

include $(TOPDIR)/Rules.make
