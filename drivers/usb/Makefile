#
# Makefile for the kernel USB device drivers.
#

O_TARGET	:= usbdrv.o

# Object files in subdirectories
mod-subdirs	:= core host class media net serial scanner misc

subdir-$(CONFIG_USB)		+= core
ifeq ($(CONFIG_USB),y)
	obj-y += core/usb-core.o
endif

subdir-$(CONFIG_USB_EHCI_HCD)	+= host
subdir-$(CONFIG_USB_OHCI_HCD)	+= host
subdir-$(CONFIG_USB_UHCI)	+= host
subdir-$(CONFIG_USB_UHCI_ALT)	+= host
subdir-$(CONFIG_USB_OHCI)	+= host
ifeq ($(CONFIG_USB_HOST),y)
	obj-y += host/usb-host.o
endif

subdir-$(CONFIG_USB_ACM)	+= class
subdir-$(CONFIG_USB_AUDIO)	+= class
subdir-$(CONFIG_USB_BLUETOOTH)	+= class
subdir-$(CONFIG_USB_CDCETHER)	+= class
subdir-$(CONFIG_USB_HID)	+= class
subdir-$(CONFIG_USB_KBD)	+= class
subdir-$(CONFIG_USB_MOUSE)	+= class
subdir-$(CONFIG_USB_PRINTER)	+= class
ifeq ($(CONFIG_USB_CLASS),y)
	obj-y += class/usb-class.o
endif

subdir-$(CONFIG_USB_STORAGE)	+= class/storage
ifeq ($(CONFIG_USB_STORAGE),y)
	obj-y += class/storage/storage.o
endif

subdir-$(CONFIG_USB_IBMCAM)	+= media
subdir-$(CONFIG_USB_KONICAWC)	+= media
subdir-$(CONFIG_USB_PWC)	+= media
subdir-$(CONFIG_USB_VICAM)	+= media
subdir-$(CONFIG_USB_OV511)	+= media
subdir-$(CONFIG_USB_SE401)	+= media
subdir-$(CONFIG_USB_STV680)	+= media
subdir-$(CONFIG_USB_DSBR)	+= media
ifeq ($(CONFIG_USB_MEDIA),y)
	obj-y += media/usb-media.o
endif

subdir-$(CONFIG_USB_PEGASUS)	+= net
subdir-$(CONFIG_USB_RTL8150)	+= net
subdir-$(CONFIG_USB_CATC)	+= net
subdir-$(CONFIG_USB_KAWETH)	+= net
subdir-$(CONFIG_USB_USBNET)	+= net
ifeq ($(CONFIG_USB_NET),y)
	obj-y += net/usb-net.o
endif

subdir-$(CONFIG_USB_MICROTEK)	+= scanner
subdir-$(CONFIG_USB_HPUSBSCSI)	+= scanner
subdir-$(CONFIG_USB_SCANNER)	+= scanner
ifeq ($(CONFIG_USB_MICROTEK),y)
	obj-y += scanner/usb-scanner.o
endif
ifeq ($(CONFIG_USB_HPUSBSCSI),y)
	obj-y += scanner/usb-scanner.o
endif
ifeq ($(CONFIG_USB_SCANNER),y)
	obj-y += scanner/usb-scanner.o
endif

subdir-$(CONFIG_USB_SERIAL)	+= serial
ifeq ($(CONFIG_USB_SERIAL),y)
	obj-y += serial/usb-serial.o
endif

subdir-$(CONFIG_USB_AUERSWALD)	+= misc
subdir-$(CONFIG_USB_DABUSB)	+= misc
subdir-$(CONFIG_USB_DC2XX)	+= misc
subdir-$(CONFIG_USB_EMI26)	+= misc
subdir-$(CONFIG_USB_MDC800)	+= misc
subdir-$(CONFIG_USB_RIO500)	+= misc
subdir-$(CONFIG_USB_TIGL)	+= misc
subdir-$(CONFIG_USB_USS720)	+= misc
subdir-$(CONFIG_USB_WACOM)	+= misc


include $(TOPDIR)/Rules.make
