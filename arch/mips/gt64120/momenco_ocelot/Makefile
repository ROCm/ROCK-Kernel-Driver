#
# Makefile for Momentum's Ocelot board.
#

EXTRA_AFLAGS := $(CFLAGS)

obj-y	 += int-handler.o irq.o pci.o prom.o reset.o setup.o

obj-$(CONFIG_REMOTE_DEBUG) += dbg_io.o
