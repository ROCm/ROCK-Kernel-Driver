#
# Makefile for the kernel serial device drivers.
#
#  $Id: Makefile,v 1.7 2002/07/06 17:16:24 rmk Exp $
#

export-objs	:= serial_core.o serial_8250.o

serial-8250-y :=
serial-8250-$(CONFIG_PCI) += serial_8250_pci.o
serial-8250-$(CONFIG_ISAPNP) += serial_8250_pnp.o
obj-$(CONFIG_SERIAL_CORE) += serial_core.o
obj-$(CONFIG_SERIAL_21285) += serial_21285.o
obj-$(CONFIG_SERIAL_8250) += serial_8250.o $(serial-8250-y)
obj-$(CONFIG_SERIAL_8250_CS) += serial_8250_cs.o
obj-$(CONFIG_SERIAL_ANAKIN) += serial_anakin.o
obj-$(CONFIG_SERIAL_AMBA) += serial_amba.o
obj-$(CONFIG_SERIAL_CLPS711X) += serial_clps711x.o
obj-$(CONFIG_SERIAL_SA1100) += serial_sa1100.o
obj-$(CONFIG_SERIAL_UART00) += serial_uart00.o

include $(TOPDIR)/Rules.make
