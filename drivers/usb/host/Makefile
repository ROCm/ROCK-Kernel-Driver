#
# Makefile for USB Host Controller Driver
# framework and drivers
#

obj-$(CONFIG_USB_EHCI_HCD)	+= ehci-hcd.o
obj-$(CONFIG_USB_OHCI_HCD)	+= ohci-hcd.o
obj-$(CONFIG_USB_UHCI_HCD)	+= usb-uhci-hcd.o
obj-$(CONFIG_USB_UHCI_HCD_ALT)	+= uhci-hcd.o

obj-$(CONFIG_USB_UHCI)		+= usb-uhci.o
obj-$(CONFIG_USB_UHCI_ALT)	+= uhci.o
obj-$(CONFIG_USB_OHCI)		+= usb-ohci.o

include $(TOPDIR)/Rules.make
