#
# Makefile for NEC DDB-Vrc5477 board
#

EXTRA_AFLAGS := $(CFLAGS)

obj-y	 += int-handler.o irq.o irq_5477.o setup.o pci.o pci_ops.o

obj-$(CONFIG_LL_DEBUG) 		+= debug.o
obj-$(CONFIG_REMOTE_DEBUG)	+= kgdb_io.o
obj-$(CONFIG_BLK_DEV_INITRD)	+= ramdisk.o

include $(TOPDIR)/Rules.make
