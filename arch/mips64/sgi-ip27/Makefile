#
# Makefile for the IP27 specific kernel interface routines under Linux.
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

O_TARGET = ip27.a
O_OBJS  = ip27-berr.o ip27-console.o ip27-irq.o ip27-init.o ip27-irq-glue.o \
	  ip27-klconfig.o ip27-klnuma.o ip27-memory.o ip27-nmi.o ip27-pci.o \
	  ip27-pci-dma.o ip27-reset.o ip27-setup.o ip27-timer.o

include $(TOPDIR)/Rules.make
