#
# Makefile for the Linux/SuperH SH-4 backends.
#

obj-y	:= fpu.o

obj-$(CONFIG_CPU_SUBTYPE_ST40STB1)	+= irq_intc2.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= irq_intc2.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_CPU_SUBTYPE_ST40STB1)	+= pci-st40.o
obj-$(CONFIG_CPU_SUBTYPE_SH7751)	+= pci-sh7751.o 
endif

