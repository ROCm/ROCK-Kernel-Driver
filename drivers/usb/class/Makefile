#
# Makefile for USB Class drivers
#

O_TARGET	:= usb-class.o

# Multipart objects.
hid-objs	:= hid-core.o

# Optional parts of multipart objects.
ifeq ($(CONFIG_USB_HIDDEV),y)
	hid-objs        += hiddev.o
endif
ifeq ($(CONFIG_USB_HIDINPUT),y)
	hid-objs        += hid-input.o
endif
                 

obj-$(CONFIG_USB_ACM)		+= cdc-acm.o
obj-$(CONFIG_USB_AUDIO)		+= audio.o
obj-$(CONFIG_USB_BLUETOOTH)	+= bluetooth.o
obj-$(CONFIG_USB_CDCETHER)	+= cdc-ether.o
obj-$(CONFIG_USB_HID)		+= hid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o
obj-$(CONFIG_USB_PRINTER)	+= printer.o


include $(TOPDIR)/Rules.make
