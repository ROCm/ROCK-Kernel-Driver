#
#  Copyright 2000 MontaVista Software Inc.
#  Author: MontaVista Software, Inc.
#     	ppopov@mvista.com or source@mvista.com
#
# Makefile for the ITE 8172 (qed-4n-s01b) board, board
# specific files.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: ite.o

O_TARGET := ite.o

obj-y := init.o 

ifdef CONFIG_PCI
obj-y += pci_fixup.o
endif

ifdef CONFIG_BLK_DEV_INITRD
obj-y += le_ramdisk.o
endif


dep:
	$(CPP) -M *.c > .depend

include $(TOPDIR)/Rules.make
