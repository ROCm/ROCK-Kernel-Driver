# Makefile for the eicon ISDN device driver

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DRV_EICON_OLD)    	+= eicon.o
obj-$(CONFIG_ISDN_DRV_EICON_DIVAS)	+= divas.o

# Multipart objects.

eicon-y					:= eicon_mod.o eicon_isa.o eicon_pci.o \
					   eicon_idi.o eicon_io.o
eicon-$(CONFIG_ISDN_DRV_EICON_PCI)	+= common.o idi.o bri.o pri.o log.o \
		   			   xlog.o kprintf.o fpga.o fourbri.o lincfg.o \
					   linchr.o linsys.o linio.o

divas-y					:= common.o idi.o bri.o pri.o log.o xlog.o \
					   kprintf.o fpga.o fourbri.o lincfg.o \
					   linchr.o linsys.o linio.o Divas_mod.o

