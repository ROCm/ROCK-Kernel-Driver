#
# Makefile for Linux arch/m68k/sun3 source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) $(AFLAGS) -traditional -Wa,-m68020 -c $< -o $*.o

O_TARGET := sun3.o 

export-objs	:= sun3_ksyms.o

obj-y		:= config.o idprom.o mmu_emu.o sun3ints.o leds.o dvma.o \
			sbus.o intersil.o sun3_ksyms.o

include $(TOPDIR)/Rules.make
