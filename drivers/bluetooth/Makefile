#
# Makefile for Bluetooth HCI device drivers.
#

O_TARGET	:= bluetooth.o

obj-$(CONFIG_BLUEZ_HCIUSB)	+= hci_usb.o
obj-$(CONFIG_BLUEZ_HCIUART)	+= hci_uart.o
obj-$(CONFIG_BLUEZ_HCIVHCI)	+= hci_vhci.o

include $(TOPDIR)/Rules.make
