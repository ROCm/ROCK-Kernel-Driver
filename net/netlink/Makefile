#
# Makefile for the netlink driver.
#

export-objs := af_netlink.o

obj-y  				:= af_netlink.o
obj-$(CONFIG_NETLINK_DEV)	+= netlink_dev.o

include $(TOPDIR)/Rules.make
