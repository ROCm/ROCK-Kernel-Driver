#
# Makefile for the kernel serial device drivers.
#
#  $Id: Makefile,v 1.8 2002/07/21 21:32:30 rmk Exp $
#

export-objs	:= core.o 8250.o sunsu.o

serial-8250-y :=
serial-8250-$(CONFIG_PCI) += 8250_pci.o
serial-8250-$(CONFIG_ISAPNP) += 8250_pnp.o
obj-$(CONFIG_SERIAL_CORE) += core.o
obj-$(CONFIG_SERIAL_21285) += 21285.o
obj-$(CONFIG_SERIAL_8250) += 8250.o $(serial-8250-y)
obj-$(CONFIG_SERIAL_8250_CS) += 8250_cs.o
obj-$(CONFIG_SERIAL_ANAKIN) += anakin.o
obj-$(CONFIG_SERIAL_AMBA) += amba.o
obj-$(CONFIG_SERIAL_CLPS711X) += clps711x.o
obj-$(CONFIG_SERIAL_SA1100) += sa1100.o
obj-$(CONFIG_SERIAL_UART00) += uart00.o
obj-$(CONFIG_SERIAL_SUNCORE) += suncore.o
obj-$(CONFIG_SERIAL_SUNZILOG) += sunzilog.o
obj-$(CONFIG_SERIAL_SUNSU) += sunsu.o
obj-$(CONFIG_SERIAL_SUNSAB) += sunsab.o

include $(TOPDIR)/Rules.make
