#
# Makefile for Momentum Computer's Ocelot-G board.
#

obj-y	 		+= gt-irq.o pci-irq.o int-handler.o irq.o prom.o \
			   reset.o setup.o
obj-$(CONFIG_KGDB)	+= dbg_io.o

EXTRA_AFLAGS := $(CFLAGS)
