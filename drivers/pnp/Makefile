#
# Makefile for the kernel Plug-and-Play device drivers.
#

O_TARGET := pnp.o

export-objs := isapnp.o pnpbios_core.o

isa-pnp-proc-$(CONFIG_PROC_FS) = isapnp_proc.o
pnpbios-proc-$(CONFIG_PROC_FS) = pnpbios_proc.o

isa-pnp-objs := isapnp.o quirks.o $(isa-pnp-proc-y)
pnpbios-objs := pnpbios_core.o $(pnpbios-proc-y)

obj-$(CONFIG_ISAPNP) += isa-pnp.o
obj-$(CONFIG_PNPBIOS) += pnpbios.o

include $(TOPDIR)/Rules.make
