#
# Makefile for the Linux kernel pci hotplug controller drivers.
#

obj-$(CONFIG_HOTPLUG_PCI)		+= pci_hotplug.o
obj-$(CONFIG_HOTPLUG_PCI_FAKE)		+= fakephp.o 
obj-$(CONFIG_HOTPLUG_PCI_COMPAQ)	+= cpqphp.o
obj-$(CONFIG_HOTPLUG_PCI_IBM)		+= ibmphp.o
obj-$(CONFIG_HOTPLUG_PCI_ACPI)		+= acpiphp.o
obj-$(CONFIG_HOTPLUG_PCI_CPCI_ZT5550)	+= cpcihp_zt5550.o
obj-$(CONFIG_HOTPLUG_PCI_CPCI_GENERIC)	+= cpcihp_generic.o

pci_hotplug-objs	:=	pci_hotplug_core.o

ifdef CONFIG_HOTPLUG_PCI_CPCI
pci_hotplug-objs	+=	cpci_hotplug_core.o	\
				cpci_hotplug_pci.o
endif

cpqphp-objs		:=	cpqphp_core.o	\
				cpqphp_ctrl.o	\
				cpqphp_sysfs.o	\
				cpqphp_pci.o

ibmphp-objs		:=	ibmphp_core.o	\
				ibmphp_ebda.o	\
				ibmphp_pci.o	\
				ibmphp_res.o	\
				ibmphp_hpc.o

acpiphp-objs		:=	acpiphp_core.o	\
				acpiphp_glue.o	\
				acpiphp_pci.o	\
				acpiphp_res.o

ifdef CONFIG_HOTPLUG_PCI_ACPI
  EXTRA_CFLAGS  += -D_LINUX -I$(TOPDIR)/drivers/acpi
  ifdef CONFIG_ACPI_DEBUG
    EXTRA_CFLAGS += -DACPI_DEBUG_OUTPUT
  endif
endif

ifeq ($(CONFIG_HOTPLUG_PCI_COMPAQ_NVRAM),y)
	cpqphp-objs += cpqphp_nvram.o
endif
