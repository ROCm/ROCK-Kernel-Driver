#
# Makefile for the S/390 common i/o drivers
#

obj-y := cio_debug.o # make sure this always comes first
obj-y += airq.o blacklist.o cio.o ioinfo.o misc.o requestirq.o s390io.o

obj-$(CONFIG_CHSC) += chsc.o
obj-$(CONFIG_PROC_FS) += proc.o

export-objs += airq.o cio.o ioinfo.o requestirq.o s390io.o

include $(TOPDIR)/Rules.make
