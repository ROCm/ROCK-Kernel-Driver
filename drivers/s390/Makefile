#
# Makefile for the S/390 specific device drivers
#

export-objs := s390dyn.o qdio.o

obj-$(CONFIG_QDIO) += qdio.o

obj-y += s390mach.o s390dyn.o sysinfo.o
obj-y += block/ char/ misc/ net/ cio/

include $(TOPDIR)/Rules.make
