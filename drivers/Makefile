#
# Makefile for the Linux kernel device drivers.
#
# 15 Sep 2000, Christoph Hellwig <hch@caldera.de>
# Rewritten to use lists instead of if-statements.
#


mod-subdirs :=	dio mtd sbus video macintosh usb input telephony sgi i2o ide \
		scsi md ieee1394 pnp isdn atm fc4 net/hamradio i2c acpi

subdir-y :=	block char net parport sound misc media cdrom
subdir-m :=	$(subdir-y)


subdir-$(CONFIG_DIO)		+= dio
subdir-$(CONFIG_PCI)		+= pci
subdir-$(CONFIG_PCMCIA)		+= pcmcia
subdir-$(CONFIG_MTD)		+= mtd
subdir-$(CONFIG_SBUS)		+= sbus
subdir-$(CONFIG_ZORRO)		+= zorro
subdir-$(CONFIG_NUBUS)		+= nubus
subdir-$(CONFIG_TC)		+= tc
subdir-$(CONFIG_VT)		+= video
subdir-$(CONFIG_MAC)		+= macintosh
subdir-$(CONFIG_ALL_PPC)	+= macintosh
subdir-$(CONFIG_USB)		+= usb
subdir-$(CONFIG_INPUT)		+= input
subdir-$(CONFIG_PHONE)		+= telephony
subdir-$(CONFIG_SGI)		+= sgi
subdir-$(CONFIG_IDE)		+= ide
subdir-$(CONFIG_SCSI)		+= scsi
subdir-$(CONFIG_I2O)		+= i2o
subdir-$(CONFIG_MD)		+= md
subdir-$(CONFIG_IEEE1394)	+= ieee1394
subdir-$(CONFIG_PNP)		+= pnp
subdir-$(CONFIG_ISDN)		+= isdn
subdir-$(CONFIG_ATM)		+= atm
subdir-$(CONFIG_FC4)		+= fc4

# CONFIG_HAMRADIO can be set without CONFIG_NETDEVICE being set  -- ch
subdir-$(CONFIG_HAMRADIO)	+= net/hamradio
subdir-$(CONFIG_I2C)		+= i2c
subdir-$(CONFIG_ACPI)		+= acpi

include $(TOPDIR)/Rules.make

