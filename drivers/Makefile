#
# Makefile for the Linux kernel device drivers.
#
# 15 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

obj-$(CONFIG_PCI)		+= pci/
obj-$(CONFIG_ACPI)		+= acpi/
obj-$(CONFIG_PARPORT)		+= parport/
obj-y 				+= base/ char/ block/ misc/ net/ media/
obj-$(CONFIG_NUBUS)		+= nubus/
obj-$(CONFIG_ATM)		+= atm/
obj-$(CONFIG_IDE)		+= ide/
obj-$(CONFIG_FC4)		+= fc4/
obj-$(CONFIG_SCSI)		+= scsi/
obj-$(CONFIG_FUSION)		+= message/
obj-$(CONFIG_IEEE1394)		+= ieee1394/
obj-y				+= cdrom/
obj-$(CONFIG_MTD)		+= mtd/
obj-$(CONFIG_PCMCIA)		+= pcmcia/
obj-$(CONFIG_DIO)		+= dio/
obj-$(CONFIG_SBUS)		+= sbus/
obj-$(CONFIG_ZORRO)		+= zorro/
obj-$(CONFIG_ALL_PPC)		+= macintosh/
obj-$(CONFIG_MAC)		+= macintosh/
obj-$(CONFIG_PNP)		+= pnp/
obj-$(CONFIG_SGI)		+= sgi/
obj-$(CONFIG_VT)		+= video/
obj-$(CONFIG_PARIDE) 		+= block/paride/
obj-$(CONFIG_TC)		+= tc/
obj-$(CONFIG_USB)		+= usb/
obj-$(CONFIG_INPUT)		+= input/
obj-$(CONFIG_GAMEPORT)		+= input/gameport/
obj-$(CONFIG_SERIO)		+= input/serio/
obj-$(CONFIG_I2O)		+= message/
obj-$(CONFIG_I2C)		+= i2c/
obj-$(CONFIG_PHONE)		+= telephony/
obj-$(CONFIG_MD)		+= md/
obj-$(CONFIG_BLUEZ)		+= bluetooth/
obj-$(CONFIG_HOTPLUG_PCI)	+= hotplug/
obj-$(CONFIG_ISDN_BOOL)		+= isdn/

include $(TOPDIR)/Rules.make
