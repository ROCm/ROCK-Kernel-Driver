#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the ITE 8172 (qed-4n-s01b) board, generic files.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: it8172.o

O_TARGET := it8172.o

obj-y := it8172_rtc.o it8172_setup.o irq.o int-handler.o pmon_prom.o time.o lpc.o puts.o reset.o

ifdef CONFIG_PCI
obj-y += it8172_pci.o
endif

ifdef CONFIG_IT8172_CIR
obj-y += it8172_cir.o 
endif

ifdef CONFIG_REMOTE_DEBUG
 obj-y += dbg_io.o             
endif

include $(TOPDIR)/Rules.make
