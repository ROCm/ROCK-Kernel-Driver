#
# Makefile for Etrax-specific drivers
#

O_TARGET := drivers.o

obj-y :=


obj-$(CONFIG_ETRAX_ETHERNET)           += ethernet.o
obj-$(CONFIG_ETRAX_SERIAL)             += serial.o
obj-$(CONFIG_ETRAX_IDE)                += ide.o
obj-$(CONFIG_ETRAX_AXISFLASHMAP)       += axisflashmap.o
obj-$(CONFIG_ETRAX_I2C) 	       += i2c.o
obj-$(CONFIG_ETRAX_GPIO) 	       += gpio.o
obj-$(CONFIG_ETRAX_USB_HOST)           += usb-host.o
obj-$(CONFIG_ETRAX_SYNCHRONOUS_SERIAL) += sync_serial.o
obj-$(CONFIG_JULIETTE)                 += juliette/juliette.o
subdir-$(CONFIG_JULIETTE)              += juliette

include $(TOPDIR)/Rules.make
