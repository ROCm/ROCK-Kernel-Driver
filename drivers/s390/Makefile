#
# Makefile for the S/390 specific device drivers
#

mod-subdirs := block char misc net

export-objs += ccwcache.o idals.o s390dyn.o s390io.o

obj-y += s390io.o s390mach.o s390dyn.o idals.o ccwcache.o
obj-y += block/ char/ misc/ net/

include $(TOPDIR)/Rules.make
