#
# Makefile for the Bluetooth subsystem
#

export-objs	:= syms.o

obj-$(CONFIG_BLUEZ) += bluez.o
obj-$(CONFIG_BLUEZ_L2CAP) += l2cap.o
obj-$(CONFIG_BLUEZ_SCO) += sco.o

subdir-$(CONFIG_BLUEZ_BNEP) += bnep

ifeq ($(CONFIG_BLUEZ_BNEP),y)
obj-y += bnep/bnep.o
endif

subdir-$(CONFIG_BLUEZ_RFCOMM) += rfcomm

ifeq ($(CONFIG_BLUEZ_RFCOMM),y)
obj-y += rfcomm/rfcomm.o
endif

bluez-objs	:= af_bluetooth.o hci_core.o hci_conn.o hci_event.o hci_sock.o lib.o syms.o

include $(TOPDIR)/Rules.make
