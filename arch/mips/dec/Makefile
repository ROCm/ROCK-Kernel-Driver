#
# Makefile for the DECstation family specific parts of the kernel
#

export-objs := wbflush.o

obj-y	 := int-handler.o setup.o irq.o time.o reset.o rtc-dec.o wbflush.o

obj-$(CONFIG_PROM_CONSOLE)	+= promcon.o

include $(TOPDIR)/Rules.make
