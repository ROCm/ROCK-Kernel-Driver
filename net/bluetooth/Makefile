#
# Makefile for the Bluetooth subsystem
#
O_TARGET	:= bluetooth.o

list-multi	:= bluez.o
export-objs	:= syms.o
bluez-objs	:= af_bluetooth.o hci_core.o hci_conn.o hci_event.o hci_sock.o lib.o syms.o

obj-$(CONFIG_BLUEZ) += bluez.o
obj-$(CONFIG_BLUEZ_L2CAP) += l2cap.o
obj-$(CONFIG_BLUEZ_SCO) += sco.o

include $(TOPDIR)/Rules.make

bluez.o: $(bluez-objs)
	$(LD) -r -o $@ $(bluez-objs)
