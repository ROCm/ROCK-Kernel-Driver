#
# Makefile for USB Core files and filesystem
#

O_TARGET	:= core.o

export-objs	:= usb.o hcd.o

usbcore-objs	:= usb.o usb-debug.o hub.o hcd.o

ifeq ($(CONFIG_USB_DEVICEFS),y)
	usbcore-objs	+= devio.o inode.o drivers.o devices.o
endif

obj-$(CONFIG_USB)	+= usbcore.o

include $(TOPDIR)/Rules.make
