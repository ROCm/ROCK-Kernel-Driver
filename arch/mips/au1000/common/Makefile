#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the Alchemy Au1000 CPU, generic files.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y				+= prom.o int-handler.o dma.o irq.o puts.o \
				   time.o reset.o clocks.o power.o

obj-$(CONFIG_AU1X00_USB_DEVICE)	+= usbdev.o
obj-$(CONFIG_KGDB)		+= dbg_io.o
obj-$(CONFIG_RTC)		+= rtc.o
