#
# Makefile for Bluetooth HCI device drivers.
#

O_TARGET	:= bluetooth.o

list-multi	:= hci_uart.o

obj-$(CONFIG_BLUEZ_HCIUSB)	+= hci_usb.o
obj-$(CONFIG_BLUEZ_HCIVHCI)	+= hci_vhci.o

obj-$(CONFIG_BLUEZ_HCIUART)	+= hci_uart.o
uart-y				:= hci_ldisc.o
uart-$(CONFIG_BLUEZ_HCIUART_H4)	+= hci_h4.o

obj-$(CONFIG_BLUEZ_HCIDTL1)	+= dtl1_cs.o
obj-$(CONFIG_BLUEZ_HCIBLUECARD)	+= bluecard_cs.o

include $(TOPDIR)/Rules.make

hci_uart.o: $(uart-y)
	$(LD) -r -o $@ $(uart-y)
