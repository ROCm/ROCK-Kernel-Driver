O_TARGET        := pci.o

obj-y		:= dma.o i386.o

ifdef	CONFIG_VISWS
obj-y		+= visws.o
else

obj-$(CONFIG_PCI_BIOS)		+= pcbios.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o

ifdef	CONFIG_MULTIQUAD
obj-y		+= numa.o
else
obj-y		+= fixup.o

ifdef	CONFIG_ACPI_PCI
obj-y		+= acpi.o
endif
obj-y		+= legacy.o


endif		# CONFIG_MULTIQUAD
obj-y		+= irq.o common.o
endif		# CONFIG_VISWS

export-objs     +=      $(obj-y)

include $(TOPDIR)/Rules.make
