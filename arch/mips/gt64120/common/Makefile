#
# Makefile for common code of gt64120-based boards.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

O_TARGET:= gt64120.o

obj-y	 := gt_irq.o pci.o

include $(TOPDIR)/Rules.make
