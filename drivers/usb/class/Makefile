#
# Makefile for USB Class drivers
# (one step up from the misc category)
#

O_TARGET	:= usb-class.o

obj-$(CONFIG_USB_ACM)		+= cdc-acm.o
obj-$(CONFIG_USB_AUDIO)		+= audio.o
obj-$(CONFIG_USB_BLUETOOTH)	+= bluetooth.o
obj-$(CONFIG_USB_PRINTER)	+= printer.o


include $(TOPDIR)/Rules.make
