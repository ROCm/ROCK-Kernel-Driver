#
# Makefile for the common code of NEC DDB-Vrc5xxx board
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

O_TARGET:= ddb5xxx.o

obj-y	 += irq.o irq_cpu.o nile4.o prom.o pci.o pci_auto.o rtc_ds1386.o

include $(TOPDIR)/Rules.make
