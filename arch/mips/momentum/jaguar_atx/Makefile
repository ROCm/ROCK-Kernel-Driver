#
# Makefile for Momentum Computer's Jaguar-ATX board.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y	 		+= mv-irq.o int-handler.o irq.o pci-irq.o prom.o reset.o setup.o
obj-$(CONFIG_PCI)	+= pci.o

obj-$(CONFIG_REMOTE_DEBUG) += dbg_io.o
