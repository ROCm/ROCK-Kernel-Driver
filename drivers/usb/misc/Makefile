#
# Makefile for the rest of the USB drivers
# (the ones that don't fit into any other categories)
#

O_TARGET	:= usb-misc.o

obj-$(CONFIG_USB_AUERSWALD)	+= auerswald.o
obj-$(CONFIG_USB_DABUSB)	+= dabusb.o
obj-$(CONFIG_USB_DC2XX)		+= dc2xx.o
obj-$(CONFIG_USB_EMI26)		+= emi26.o
obj-$(CONFIG_USB_MDC800)	+= mdc800.o
obj-$(CONFIG_USB_RIO500)	+= rio500.o
obj-$(CONFIG_USB_TIGL)		+= tiglusb.o
obj-$(CONFIG_USB_USS720)	+= uss720.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o

include $(TOPDIR)/Rules.make
