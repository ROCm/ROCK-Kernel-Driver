O_TARGET        := pci.o

obj-y		:= x86-64.o

obj-$(CONFIG_PCI_DIRECT)	+= direct.o

obj-y		+= fixup.o

ifdef	CONFIG_ACPI_PCI
obj-y		+= acpi.o
endif
obj-y		+= legacy.o


obj-y		+= irq.o common.o

include $(TOPDIR)/Rules.make
