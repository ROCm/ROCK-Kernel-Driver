#
# Makefile for the kernel ISAPNP driver.
#

export-objs := core.o

isapnp-proc-$(CONFIG_PROC_FS) = proc.o

obj-y := core.o $(isapnp-proc-y)
