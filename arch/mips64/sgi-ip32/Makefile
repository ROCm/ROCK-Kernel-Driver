#
# Makefile for the SGI specific kernel interface routines
# under Linux.
#

EXTRA_TARGETS	:= ip32-irq-glue.o

obj-y		+= ip32-irq.o ip32-rtc.o ip32-setup.o ip32-irq-glue.o \
	 	   ip32-berr.o ip32-timer.o crime.o

obj-$(CONFIG_PCI) += ip32-pci.o ip32-pci-dma.o

EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make
