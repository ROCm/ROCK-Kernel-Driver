#
# Makefile for the kernel PNPBIOS driver.
#

export-objs := core.o

pnpbios-proc-$(CONFIG_PROC_FS) = proc.o

obj-y := core.o $(pnpbios-proc-y)
