#
# Makefile for the S/390 specific device drivers
#

mod-subdirs := block char misc net scsi cio

export-objs += s390dyn.o

obj-$(CONFIG_QDIO) += qdio.o
export-objs += qdio.o

obj-y += s390mach.o s390dyn.o sysinfo.o
obj-y += block/ char/ misc/ net/ scsi/ cio/

include $(TOPDIR)/Rules.make
