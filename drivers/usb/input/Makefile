#
# Makefile for the USB input drivers
#

# Multipart objects.
hid-objs	:= hid-core.o

# Optional parts of multipart objects.
ifeq ($(CONFIG_USB_HIDDEV),y)
	hid-objs        += hiddev.o
endif
ifeq ($(CONFIG_USB_HIDINPUT),y)
	hid-objs        += hid-input.o
endif

obj-$(CONFIG_USB_HID)		+= hid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o


include $(TOPDIR)/Rules.make
