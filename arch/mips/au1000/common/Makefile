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

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: au1000.o

O_TARGET := au1000.o

obj-y := prom.o dbg_io.o int-handler.o irq.o puts.o time.o reset.o

obj-$(CONFIG_AU1000_UART) += serial.o             
obj-$(CONFIG_REMOTE_DEBUG) += dbg_io.o             
obj-$(CONFIG_BLK_DEV_INITRD) += ramdisk.o

ramdisk.o: 
	mkramobj ramdisk ramdisk.o

include $(TOPDIR)/Rules.make
