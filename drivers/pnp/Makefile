#
# Makefile for the Linux Plug-and-Play Support.
#

obj-y		:= core.o driver.o resource.o interface.o quirks.o names.o compat.o system.o

obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

export-objs	:= core.o driver.o resource.o compat.o

include $(TOPDIR)/Rules.make
