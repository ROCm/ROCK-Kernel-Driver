#
# Makefile for the linux kernel.
#

obj-y   := evtchn.o fixup.o reboot.o gnttab.o devmem.o

obj-$(CONFIG_PROC_FS) += xen_proc.o
obj-$(CONFIG_NET)     += skbuff.o
obj-$(CONFIG_SMP)     += smp.o smpboot.o
