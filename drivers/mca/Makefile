# Makefile for the Linux MCA bus support

obj-y	:= mca-bus.o mca-device.o mca-driver.o

obj-$(CONFIG_MCA_PROC_FS)	+= mca-proc.o
obj-$(CONFIG_MCA_LEGACY)	+= mca-legacy.o

export-objs	:= mca-bus.o mca-legacy.o mca-proc.o mca-driver.o
