#
# Makefile for NEC DDB-Vrc5477 board
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

USE_STANDARD_AS_RULE := true

EXTRA_AFLAGS := $(CFLAGS)

O_TARGET:= ddb5477.o

obj-y	 += int-handler.o irq.o irq_5477.o setup.o pci.o pci_ops.o

obj-$(CONFIG_LL_DEBUG) 		+= debug.o
obj-$(CONFIG_REMOTE_DEBUG)	+= kgdb_io.o
obj-$(CONFIG_BLK_DEV_INITRD)	+= ramdisk.o

include $(TOPDIR)/Rules.make
