# Makefile for the NCR/SYMBIOS/LSI 53C8XX PCI SCSI controllers driver.

O_TARGET := sym53c8xx_2.o

sym53c8xx-objs := sym_fw.o sym_glue.o sym_hipd.o sym_malloc.o sym_misc.o sym_nvram.o
obj-$(CONFIG_SCSI_SYM53C8XX_2) := sym53c8xx.o

EXTRA_CFLAGS += -I.

include $(TOPDIR)/Rules.make

clean:
	rm -f *.o
