#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the Alchemy Au1000 CPU, generic files.
#

obj-y += prom.o int-handler.o irq.o puts.o time.o reset.o \
	au1xxx_irqmap.o clocks.o power.o setup.o sleeper.o

obj-$(CONFIG_AU1X00_USB_DEVICE)	+= usbdev.o
obj-$(CONFIG_KGDB)		+= dbg_io.o
obj-$(CONFIG_PCI)		+= pci.o

ifdef CONFIG_SOC_AU1550
obj-y += dbdma.o
else
obj-y += dma.o
endif
