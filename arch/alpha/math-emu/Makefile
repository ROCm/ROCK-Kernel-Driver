#
# Makefile for the FPU instruction emulation.
#

obj-$(CONFIG_MATHEMU) += math-emu.o

math-emu-objs	:= math.o qrnnd.o

CFLAGS += -I. -I$(TOPDIR)/include/math-emu -w

include $(TOPDIR)/Rules.make
