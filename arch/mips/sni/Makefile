# $Id: Makefile,v 1.3 1999/01/04 16:03:57 ralf Exp $
#
# Makefile for the SNI specific part of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: sni.o
O_TARGET := sni.o
O_OBJS	 := dma.o int-handler.o io.o pci.o pcimt_scache.o reset.o setup.o

int-handler.o: int-handler.S

clean:

include $(TOPDIR)/Rules.make
