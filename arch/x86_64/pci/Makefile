#
# Makefile for X86_64 specific PCI routines
#
obj-y		:= x86-64.o
obj-$(CONFIG_PCI_DIRECT)+= direct.o
obj-y		+= fixup.o
obj-$(CONFIG_ACPI_PCI)	+= acpi.o
obj-y			+= legacy.o irq.o common.o
