#
# Makefile for Linux arch/ppc/iSeries source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := iSeries.o
export-objs := iSeries_ksyms.o
obj-y   += LparData.o ItLpQueue.o HvLpEvent.o HvCall.o mf.o iSeries_proc.o mf_proc.o iSeries_ksyms.o HvLpConfig.o pmc_proc.o rtc.o

obj-$(CONFIG_PCI) += XmPciLpEvent.o iSeries_FlightRecorder.o iSeries_IoMmTable.o iSeries_VpdInfo.o iSeries_fixup.o iSeries_irq.o iSeries_pci.o iSeries_pci_proc.o iSeries_reset_device.o

all: iSeries.o

LparData.c:: ReleaseData.h

ReleaseData.h: $(TOPDIR)/Makefile
	/bin/bash ./createReleaseData $(KERNELRELEASE) > $@

clean:
	rm -f ReleaseData.h

include $(TOPDIR)/Rules.make
