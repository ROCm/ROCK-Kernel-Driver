#
# Makefile for the Linux Bluetooth HCI device drivers.
#

obj-$(CONFIG_BLUEZ_HCIUSB)	+= hci_usb.o
obj-$(CONFIG_BLUEZ_HCIVHCI)	+= hci_vhci.o
obj-$(CONFIG_BLUEZ_HCIUART)	+= hci_uart.o
obj-$(CONFIG_BLUEZ_HCIDTL1)	+= dtl1_cs.o
obj-$(CONFIG_BLUEZ_HCIBT3C)	+= bt3c_cs.o
obj-$(CONFIG_BLUEZ_HCIBLUECARD)	+= bluecard_cs.o

hci_uart-y				:= hci_ldisc.o
hci_uart-$(CONFIG_BLUEZ_HCIUART_H4)	+= hci_h4.o
hci_uart-$(CONFIG_BLUEZ_HCIUART_BCSP)	+= hci_bcsp.o
hci_uart-objs				:= $(hci_uart-y)

include $(TOPDIR)/Rules.make
