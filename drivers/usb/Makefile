#
# Makefile for the kernel USB device drivers.
#

# The target object and module list name.

O_TARGET	:= usbdrv.o

# Objects that export symbols.

export-objs		:= media/ov511.o media/pwc-uncompress.o

# Multipart objects.

hid-objs		:= class/hid-core.o
pwc-objs		:= media/pwc-if.o media/pwc-misc.o media/pwc-ctrl.o media/pwc-uncompress.o


# Optional parts of multipart objects.
ifeq ($(CONFIG_USB_HIDDEV),y)
	hid-objs	+= class/hiddev.o
endif

ifeq ($(CONFIG_USB_HIDINPUT),y)
	hid-objs	+= class/hid-input.o
endif

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.


# EHCI needs to be linked before the other HCD drivers
ifeq ($(CONFIG_USB_EHCI_HCD),y)
	obj-y += host/ehci-hcd.o
endif

ifeq ($(CONFIG_USB_OHCI_HCD),y)
	obj-y += host/ohci-hcd.o
endif

obj-$(CONFIG_USB_MOUSE)		+= class/usbmouse.o
obj-$(CONFIG_USB_HID)		+= class/hid.o
obj-$(CONFIG_USB_KBD)		+= class/usbkbd.o
obj-$(CONFIG_USB_ACM)		+= class/cdc-acm.o
obj-$(CONFIG_USB_PRINTER)	+= class/printer.o
obj-$(CONFIG_USB_AUDIO)		+= class/audio.o
obj-$(CONFIG_USB_CDCETHER)	+= class/cdc-ether.o
obj-$(CONFIG_USB_BLUETOOTH)	+= class/bluetooth.o

obj-$(CONFIG_USB_WACOM)		+= misc/wacom.o
obj-$(CONFIG_USB_SCANNER)	+= scanner/scanner.o
obj-$(CONFIG_USB_EMI26)		+= misc/emi26.o
obj-$(CONFIG_USB_IBMCAM)	+= media/ibmcam.o media/usbvideo.o media/ultracam.o
obj-$(CONFIG_USB_KONICAWC)	+= media/konicawc.o media/usbvideo.o
obj-$(CONFIG_USB_PWC)		+= media/pwc.o
obj-$(CONFIG_USB_DC2XX)		+= misc/dc2xx.o
obj-$(CONFIG_USB_MDC800)	+= misc/mdc800.o
obj-$(CONFIG_USB_USS720)	+= misc/uss720.o
obj-$(CONFIG_USB_DABUSB)	+= misc/dabusb.o
obj-$(CONFIG_USB_VICAM)		+= media/vicam.o
obj-$(CONFIG_USB_OV511)		+= media/ov511.o
obj-$(CONFIG_USB_SE401)		+= media/se401.o
obj-$(CONFIG_USB_STV680)	+= media/stv680.o
obj-$(CONFIG_USB_PEGASUS)	+= net/pegasus.o
obj-$(CONFIG_USB_RTL8150)	+= net/rtl8150.o
obj-$(CONFIG_USB_CATC)		+= net/catc.o
obj-$(CONFIG_USB_KAWETH)        += net/kaweth.o
obj-$(CONFIG_USB_RIO500)	+= misc/rio500.o
obj-$(CONFIG_USB_DSBR)		+= media/dsbr100.o
obj-$(CONFIG_USB_MICROTEK)	+= scanner/microtek.o
obj-$(CONFIG_USB_HPUSBSCSI)	+= scanner/hpusbscsi.o
obj-$(CONFIG_USB_USBNET)	+= net/usbnet.o
obj-$(CONFIG_USB_AUERSWALD)	+= misc/auerswald.o
obj-$(CONFIG_USB_TIGL)		+= misc/tiglusb.o

# Object files in subdirectories
mod-subdirs	:= core host serial

subdir-$(CONFIG_USB)		+= core
subdir-$(CONFIG_USB_EHCI_HCD)	+= host
subdir-$(CONFIG_USB_OHCI_HCD)	+= host
subdir-$(CONFIG_USB_UHCI)	+= host
subdir-$(CONFIG_USB_UHCI_ALT)	+= host
subdir-$(CONFIG_USB_OHCI)	+= host
subdir-$(CONFIG_USB_SERIAL)	+= serial
subdir-$(CONFIG_USB_STORAGE)	+= class/storage

ifeq ($(CONFIG_USB),y)
	obj-y += core/usb-core.o
endif

ifeq ($(CONFIG_USB_EHCI_HCD),y)
	obj-y += host/usb-host.o
endif

ifeq ($(CONFIG_USB_SERIAL),y)
	obj-y += serial/usb-serial.o
endif

ifeq ($(CONFIG_USB_STORAGE),y)
	obj-y += class/storage/storage.o
endif

include $(TOPDIR)/Rules.make
