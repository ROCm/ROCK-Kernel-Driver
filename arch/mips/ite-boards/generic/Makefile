#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the ITE 8172 (qed-4n-s01b) board, generic files.
#

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

EXTRA_AFLAGS := $(CFLAGS)
