#
# Makefile for the kernel USB device drivers.
#

O_TARGET	:= usbdrv.o

# Object files in subdirectories
mod-subdirs	:= core host class media net serial storage image misc input

subdir-$(CONFIG_USB)		+= core
ifeq ($(CONFIG_USB),y)
	obj-y += core/core.o
endif

subdir-$(CONFIG_USB_EHCI_HCD)	+= host
subdir-$(CONFIG_USB_OHCI_HCD)	+= host
subdir-$(CONFIG_USB_OHCI)	+= host
subdir-$(CONFIG_USB_UHCI_ALT)	+= host
subdir-$(CONFIG_USB_UHCI)	+= host
ifeq ($(CONFIG_USB_HOST),y)
	obj-y += host/host.o
endif

subdir-$(CONFIG_USB_ACM)	+= class
subdir-$(CONFIG_USB_AUDIO)	+= class
subdir-$(CONFIG_USB_BLUETOOTH)	+= class
subdir-$(CONFIG_USB_PRINTER)	+= class
ifeq ($(CONFIG_USB_CLASS),y)
	obj-y += class/class.o
endif

subdir-$(CONFIG_USB_STORAGE)	+= storage
ifeq ($(CONFIG_USB_STORAGE),y)
	obj-y += storage/storage.o
endif

subdir-$(CONFIG_USB_HID)	+= input
subdir-$(CONFIG_USB_KBD)	+= input
subdir-$(CONFIG_USB_MOUSE)	+= input
subdir-$(CONFIG_USB_WACOM)	+= input
ifeq ($(CONFIG_USB_INPUT),y)
	obj-y += input/input.o
endif

subdir-$(CONFIG_USB_DABUSB)	+= media
subdir-$(CONFIG_USB_DSBR)	+= media
subdir-$(CONFIG_USB_IBMCAM)	+= media
subdir-$(CONFIG_USB_KONICAWC)	+= media
subdir-$(CONFIG_USB_OV511)	+= media
subdir-$(CONFIG_USB_PWC)	+= media
subdir-$(CONFIG_USB_SE401)	+= media
subdir-$(CONFIG_USB_STV680)	+= media
subdir-$(CONFIG_USB_VICAM)	+= media
ifeq ($(CONFIG_USB_MEDIA),y)
	obj-y += media/media.o
endif

subdir-$(CONFIG_USB_CATC)	+= net
subdir-$(CONFIG_USB_CDCETHER)	+= net
subdir-$(CONFIG_USB_KAWETH)	+= net
subdir-$(CONFIG_USB_PEGASUS)	+= net
subdir-$(CONFIG_USB_RTL8150)	+= net
subdir-$(CONFIG_USB_USBNET)	+= net
ifeq ($(CONFIG_USB_NET),y)
	obj-y += net/net.o
endif

subdir-$(CONFIG_USB_DC2XX)	+= image
subdir-$(CONFIG_USB_HPUSBSCSI)	+= image
subdir-$(CONFIG_USB_MDC800)	+= image
subdir-$(CONFIG_USB_MICROTEK)	+= image
subdir-$(CONFIG_USB_SCANNER)	+= image
ifeq ($(CONFIG_USB_IMAGE),y)
	obj-y += image/image.o
endif

subdir-$(CONFIG_USB_SERIAL)	+= serial
ifeq ($(CONFIG_USB_SERIAL),y)
	obj-y += serial/serial.o
endif

subdir-$(CONFIG_USB_AUERSWALD)	+= misc
subdir-$(CONFIG_USB_EMI26)	+= misc
subdir-$(CONFIG_USB_RIO500)	+= misc
subdir-$(CONFIG_USB_TIGL)	+= misc
subdir-$(CONFIG_USB_USS720)	+= misc
ifeq ($(CONFIG_USB_MISC),y)
	obj-y += misc/misc.o
endif

include $(TOPDIR)/Rules.make
