#
# Makefile for the PCI bus specific drivers.
#

export-objs := access.o hotplug.o pci-driver.o pci.o pool.o probe.o proc.o search.o

obj-y		+= access.o probe.o pci.o pool.o quirks.o \
			compat.o names.o pci-driver.o search.o
obj-$(CONFIG_PM)  += power.o
obj-$(CONFIG_HOTPLUG)  += hotplug.o

obj-$(CONFIG_PROC_FS) += proc.o

ifndef CONFIG_SPARC64
obj-$(CONFIG_PCI) += setup-res.o
endif

#
# Some architectures use the generic PCI setup functions
#
obj-$(CONFIG_ALPHA) += setup-bus.o setup-irq.o
obj-$(CONFIG_ARM) += setup-bus.o setup-irq.o
obj-$(CONFIG_PARISC) += setup-bus.o
obj-$(CONFIG_SUPERH) += setup-bus.o setup-irq.o
obj-$(CONFIG_PPC32) += setup-irq.o
obj-$(CONFIG_DDB5476) += setup-bus.o
obj-$(CONFIG_SGI_IP27) += setup-irq.o

ifndef CONFIG_X86
obj-y += syscall.o
endif

include $(TOPDIR)/Rules.make

names.o: names.c devlist.h classlist.h

devlist.h classlist.h: pci.ids gen-devlist
	./gen-devlist <pci.ids

gen-devlist: gen-devlist.c
	$(HOSTCC) $(HOSTCFLAGS) -o gen-devlist gen-devlist.c
