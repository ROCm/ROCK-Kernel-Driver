#
# Makefile for the Bluetooth subsystem
#

O_TARGET	:= bluetooth.o

export-objs	:= syms.o
hci-objs	:= af_bluetooth.o hci_core.o hci_sock.o lib.o syms.o
l2cap-objs	:= l2cap_core.o l2cap_proc.o

obj-$(CONFIG_BLUEZ) += hci.o
obj-$(CONFIG_BLUEZ_L2CAP) += l2cap.o

include $(TOPDIR)/Rules.make
