#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the Alchemy Au1000 CPU, generic files.
#

O_TARGET := au1000.o

obj-y := prom.o dbg_io.o int-handler.o irq.o puts.o time.o reset.o

obj-$(CONFIG_AU1000_UART) += serial.o             
obj-$(CONFIG_REMOTE_DEBUG) += dbg_io.o             
obj-$(CONFIG_BLK_DEV_INITRD) += ramdisk.o

EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make

ramdisk.o: 
	mkramobj ramdisk ramdisk.o
