#
# Makefile for the Linux Bluetooth subsystem.
#

export-objs := syms.o

obj-$(CONFIG_BLUEZ)		+= bluetooth.o
obj-$(CONFIG_BLUEZ_L2CAP)	+= l2cap.o
obj-$(CONFIG_BLUEZ_SCO)		+= sco.o
obj-$(CONFIG_BLUEZ_RFCOMM)	+= rfcomm/
obj-$(CONFIG_BLUEZ_BNEP)	+= bnep/

bluetooth-objs := af_bluetooth.o hci_core.o hci_conn.o hci_event.o hci_sock.o lib.o syms.o

include $(TOPDIR)/Rules.make
