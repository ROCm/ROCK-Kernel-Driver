#
# Makefile for the Bluetooth subsystem
#

export-objs	:= syms.o

obj-$(CONFIG_BLUEZ) += bluez.o
obj-$(CONFIG_BLUEZ_L2CAP) += l2cap.o
obj-$(CONFIG_BLUEZ_SCO) += sco.o

bluez-objs	:= af_bluetooth.o hci_core.o hci_conn.o hci_event.o hci_sock.o lib.o syms.o

include $(TOPDIR)/Rules.make
