#
# Makefile for Linux arch/m68k/sun3 source directory
#

export-objs	:= sun3_ksyms.o

obj-y	:= sun3_ksyms.o sun3ints.o sun3dvma.o sbus.o

obj-$(CONFIG_SUN3) += config.o idprom.o mmu_emu.o leds.o dvma.o \
			intersil.o

include $(TOPDIR)/Rules.make
