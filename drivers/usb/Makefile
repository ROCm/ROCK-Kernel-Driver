#
# Makefile for the kernel USB device drivers.
#

# Object files in subdirectories
mod-subdirs	:= serial

subdir-$(CONFIG_USB)		+= core

subdir-$(CONFIG_USB_EHCI_HCD)		+= host
subdir-$(CONFIG_USB_OHCI_HCD)		+= host
subdir-$(CONFIG_USB_OHCI)		+= host
subdir-$(CONFIG_USB_UHCI_ALT)		+= host
subdir-$(CONFIG_USB_UHCI_HCD_ALT)	+= host
subdir-$(CONFIG_USB_UHCI_HCD)		+= host
subdir-$(CONFIG_USB_UHCI)		+= host

subdir-$(CONFIG_USB_ACM)		+= class
subdir-$(CONFIG_USB_AUDIO)		+= class
subdir-$(CONFIG_USB_BLUETOOTH_TTY)	+= class
subdir-$(CONFIG_USB_PRINTER)		+= class

subdir-$(CONFIG_USB_STORAGE)	+= storage

subdir-$(CONFIG_USB_HID)	+= input
subdir-$(CONFIG_USB_KBD)	+= input
subdir-$(CONFIG_USB_MOUSE)	+= input
subdir-$(CONFIG_USB_WACOM)	+= input

subdir-$(CONFIG_USB_DABUSB)	+= media
subdir-$(CONFIG_USB_DSBR)	+= media
subdir-$(CONFIG_USB_IBMCAM)	+= media
subdir-$(CONFIG_USB_KONICAWC)	+= media
subdir-$(CONFIG_USB_OV511)	+= media
subdir-$(CONFIG_USB_PWC)	+= media
subdir-$(CONFIG_USB_SE401)	+= media
subdir-$(CONFIG_USB_STV680)	+= media
subdir-$(CONFIG_USB_VICAM)	+= media

subdir-$(CONFIG_USB_CATC)	+= net
subdir-$(CONFIG_USB_CDCETHER)	+= net
subdir-$(CONFIG_USB_KAWETH)	+= net
subdir-$(CONFIG_USB_PEGASUS)	+= net
subdir-$(CONFIG_USB_RTL8150)	+= net
subdir-$(CONFIG_USB_USBNET)	+= net

subdir-$(CONFIG_USB_DC2XX)	+= image
subdir-$(CONFIG_USB_HPUSBSCSI)	+= image
subdir-$(CONFIG_USB_MDC800)	+= image
subdir-$(CONFIG_USB_MICROTEK)	+= image
subdir-$(CONFIG_USB_SCANNER)	+= image

subdir-$(CONFIG_USB_SERIAL)	+= serial

subdir-$(CONFIG_USB_AUERSWALD)	+= misc
subdir-$(CONFIG_USB_BRLVGER)	+= misc
subdir-$(CONFIG_USB_EMI26)	+= misc
subdir-$(CONFIG_USB_RIO500)	+= misc
subdir-$(CONFIG_USB_TIGL)	+= misc
subdir-$(CONFIG_USB_USS720)	+= misc

obj-y += $(join $(subdir-y),$(subdir-y:%=/%.o))

include $(TOPDIR)/Rules.make
