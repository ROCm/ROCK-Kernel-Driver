#
# Makefile for the Linux Plug-and-Play Support.
#

pnp-card-$(CONFIG_PNP_CARD) = card.o

obj-y		:= core.o driver.o resource.o interface.o quirks.o names.o system.o $(pnp-card-y)

obj-$(CONFIG_PNPBIOS)		+= pnpbios/
obj-$(CONFIG_ISAPNP)		+= isapnp/

export-objs	:= core.o driver.o resource.o $(pnp-card-y)

include $(TOPDIR)/Rules.make
