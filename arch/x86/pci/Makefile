obj-y				:= i386.o init.o

obj-$(CONFIG_PCI_BIOS)		+= pcbios.o
obj-$(CONFIG_PCI_MMCONFIG)	+= mmconfig_$(BITS).o direct.o mmconfig-shared.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o
obj-$(CONFIG_PCI_OLPC)		+= olpc.o
# pcifront should be after mmconfig.o and direct.o as it should only
# take over if direct access to the PCI bus is unavailable
obj-$(CONFIG_XEN_PCIDEV_FRONTEND) += pcifront.o

obj-y				+= fixup.o
obj-$(CONFIG_ACPI)		+= acpi.o
obj-y				+= legacy.o irq.o

obj-$(CONFIG_X86_VISWS)		+= visws.o

obj-$(CONFIG_X86_NUMAQ)		+= numaq_32.o

obj-y				+= common.o early.o
obj-y				+= amd_bus.o
