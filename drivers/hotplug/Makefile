#
# Makefile for the Linux kernel pci hotplug controller drivers.
#

export-objs	:= pci_hotplug_core.o pci_hotplug_util.o

obj-$(CONFIG_HOTPLUG_PCI)		+= pci_hotplug.o
obj-$(CONFIG_HOTPLUG_PCI_COMPAQ)	+= cpqphp.o
obj-$(CONFIG_HOTPLUG_PCI_IBM)		+= ibmphp.o
obj-$(CONFIG_HOTPLUG_PCI_ACPI)		+= acpiphp.o

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


include $(TOPDIR)/Rules.make
