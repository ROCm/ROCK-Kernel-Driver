#
# Makefile for the Linux Bluetooth RFCOMM layer.
#

obj-$(CONFIG_BLUEZ_RFCOMM) += rfcomm.o

rfcomm-y				:= core.o sock.o crc.o
rfcomm-$(CONFIG_BLUEZ_RFCOMM_TTY)	+= tty.o
rfcomm-objs				:= $(rfcomm-y)

include $(TOPDIR)/Rules.make
