obj-y		:= i386.o

obj-$(CONFIG_PCI_BIOS)		+= pcbios.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o

obj-$(CONFIG_X86_VISWS)		+= visws.o

ifdef	CONFIG_X86_NUMAQ
obj-y		+= numa.o
else
obj-y		+= fixup.o

ifdef	CONFIG_ACPI_PCI
obj-y		+= acpi.o
endif

ifndef	CONFIG_X86_VISWS
obj-y		+= legacy.o
endif

endif		# CONFIG_X86_NUMAQ

ifndef	CONFIG_X86_VISWS
obj-y		+= irq.o
endif

obj-y		+= common.o
