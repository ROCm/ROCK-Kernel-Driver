#
# Makefile for the FPU instruction emulation.
#

CFLAGS += -I. -I$(TOPDIR)/include/math-emu -w

ifeq ($(CONFIG_MATHEMU),y)

O_TARGET	:= math-emu.o
obj-y		:= math.o qrnnd.o

else

math-emu-objs	:= math.o qrnnd.o
obj-m		:= math-emu.o

endif

include $(TOPDIR)/Rules.make
