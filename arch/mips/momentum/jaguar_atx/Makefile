#
# Makefile for Momentum Computer's Jaguar-ATX board.
#

obj-y	 		+= mv-irq.o int-handler.o irq.o pci-irq.o prom.o reset.o setup.o
obj-$(CONFIG_PCI)	+= pci.o

obj-$(CONFIG_REMOTE_DEBUG) += dbg_io.o
