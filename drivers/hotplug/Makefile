#
# Makefile for the Linux kernel pci hotplug controller drivers.
#

O_TARGET	:= vmlinux-obj.o

export-objs	:= pci_hotplug_core.o pci_hotplug_util.o

obj-$(CONFIG_HOTPLUG_PCI)		+= pci_hotplug.o
obj-$(CONFIG_HOTPLUG_PCI_COMPAQ)	+= cpqphp.o
obj-$(CONFIG_HOTPLUG_PCI_IBM)		+= ibmphp.o

pci_hotplug-objs	:=	pci_hotplug_core.o	\
				pci_hotplug_util.o

cpqphp-objs		:=	cpqphp_core.o	\
				cpqphp_ctrl.o	\
				cpqphp_proc.o	\
				cpqphp_pci.o

ibmphp-objs		:=	ibmphp_core.o	\
				ibmphp_ebda.o	\
				ibmphp_pci.o	\
				ibmphp_res.o	\
				ibmphp_hpc.o

ifeq ($(CONFIG_HOTPLUG_PCI_COMPAQ_NVRAM),y)
	cpqphp-objs += cpqphp_nvram.o
endif


include $(TOPDIR)/Rules.make
