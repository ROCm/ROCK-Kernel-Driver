#
# Makefile for the Linux kernel pci hotplug controller drivers.
#

O_TARGET	:= vmlinux-obj.o

list-multi	:= cpqphp.o pci_hotplug.o

export-objs	:= pci_hotplug_core.o pci_hotplug_util.o

obj-$(CONFIG_HOTPLUG_PCI)		+= pci_hotplug.o
obj-$(CONFIG_HOTPLUG_PCI_COMPAQ)	+= cpqphp.o

pci_hotplug-objs	:=	pci_hotplug_core.o	\
				pci_hotplug_util.o

cpqphp-objs		:=	cpqphp_core.o	\
				cpqphp_ctrl.o	\
				cpqphp_proc.o	\
				cpqphp_pci.o

ifeq ($(CONFIG_HOTPLUG_PCI_COMPAQ_NVRAM),y)
	cpqphp-objs += cpqphp_nvram.o
endif


include $(TOPDIR)/Rules.make

pci_hotplug.o: $(pci_hotplug-objs)
	$(LD) -r -o $@ $(pci_hotplug-objs)

cpqphp.o: $(cpqphp-objs)
	$(LD) -r -o $@ $(cpqphp-objs)

