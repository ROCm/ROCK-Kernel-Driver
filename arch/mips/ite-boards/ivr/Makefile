#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the Globespan IVR board,
# board-specific files.
#

O_TARGET := ivr.o

obj-y := init.o 

obj-$(CONFIG_PCI) += pci_fixup.o
obj-$(CONFIG_BLK_DEV_INITRD) += le_ramdisk.o

include $(TOPDIR)/Rules.make
