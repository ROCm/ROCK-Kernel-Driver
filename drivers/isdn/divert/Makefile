#
# Makefile for the dss1_divert ISDN module
#

# The target object and module list name.

O_TARGET	:= built-in.o

# Multipart objects.

dss1_divert-objs	:= isdn_divert.o divert_procfs.o divert_init.o

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN_DIVERSION)	+= dss1_divert.o

include $(TOPDIR)/Rules.make





