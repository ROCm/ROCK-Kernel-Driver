# Makefile for the Linux MCA bus support

obj-y	:= mca-bus.o mca-device.o mca-driver.o mca-legacy.o

obj-$(CONFIG_PROC_FS)	+= mca-proc.o

export-objs	:= mca-bus.o mca-legacy.o mca-proc.o mca-driver.o

include $(TOPDIR)/Rules.make
