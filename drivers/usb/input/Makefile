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
ifeq ($(CONFIG_HID_PID),y)
	hid-objs	+= pid.o
endif
ifeq ($(CONFIG_LOGITECH_FF),y)
	hid-objs	+= hid-lgff.o
endif
ifeq ($(CONFIG_THRUSTMASTER_FF),y)
	hid-objs	+= hid-tmff.o
endif
ifeq ($(CONFIG_HID_FF),y)
	hid-objs	+= hid-ff.o
endif

obj-$(CONFIG_USB_AIPTEK)	+= aiptek.o
obj-$(CONFIG_USB_HID)		+= hid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o
obj-$(CONFIG_USB_KBTAB)		+= kbtab.o
obj-$(CONFIG_USB_POWERMATE)	+= powermate.o
obj-$(CONFIG_USB_XPAD)		+= xpad.o
