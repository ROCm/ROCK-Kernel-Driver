#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= arch.o cpu.o irq.o mm.o time.o
obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_LEDS)	+= leds.o
obj-$(CONFIG_PCI)	+= pci_v3.o pci.o

include $(TOPDIR)/Rules.make
