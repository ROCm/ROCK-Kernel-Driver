#
# Makefile for USB Host Controller Driver
# framework and drivers
#

export-objs := usb-ohci.o

obj-$(CONFIG_USB_EHCI_HCD)	+= ehci-hcd.o
obj-$(CONFIG_USB_OHCI_HCD)	+= ohci-hcd.o
obj-$(CONFIG_USB_UHCI_HCD)	+= usb-uhci-hcd.o
obj-$(CONFIG_USB_UHCI_HCD_ALT)	+= uhci-hcd.o

obj-$(CONFIG_USB_UHCI)		+= usb-uhci.o
obj-$(CONFIG_USB_OHCI)		+= usb-ohci.o usb-ohci-pci.o
obj-$(CONFIG_USB_OHCI_SA1111)	+= usb-ohci.o usb-ohci-sa1111.o
obj-$(CONFIG_USB_SL811HS)	+= hc_sl811.o

include $(TOPDIR)/Rules.make
