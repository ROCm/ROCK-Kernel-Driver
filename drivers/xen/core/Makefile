#
# Makefile for the linux kernel.
#

obj-y := evtchn.o gnttab.o reboot.o machine_reboot.o

priv-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_XEN_PRIVILEGED_GUEST) += firmware.o $(priv-y)
obj-$(CONFIG_PROC_FS)		+= xen_proc.o
obj-$(CONFIG_SYS_HYPERVISOR)	+= hypervisor_sysfs.o
obj-$(CONFIG_HOTPLUG_CPU)	+= cpu_hotplug.o
obj-$(CONFIG_XEN_SYSFS)		+= xen_sysfs.o
obj-$(CONFIG_XEN_SMPBOOT)	+= smpboot.o
obj-$(CONFIG_SMP)		+= spinlock.o
obj-$(CONFIG_KEXEC)		+= machine_kexec.o
obj-$(CONFIG_XEN_DOMCTL)	+= domctl.o
CFLAGS_domctl.o			:= -D__XEN_TOOLS__ -D__XEN_PUBLIC_XEN_H__ -imacros xen/interface/domctl.h
obj-$(CONFIG_XEN_XENCOMM)	+= xencomm.o
