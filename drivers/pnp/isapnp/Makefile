#
# Makefile for the kernel ISAPNP driver.
#

export-objs := core.o compat.o

isapnp-proc-$(CONFIG_PROC_FS) = proc.o

obj-y := core.o compat.o $(isapnp-proc-y)
