obj-$(CONFIG_ACPI)		+= boot.o
obj-$(CONFIG_ACPI_SLEEP)	+= sleep.o wakeup.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= cstate.o processor.o
endif

ifdef CONFIG_XEN
include $(srctree)/scripts/Makefile.xen
n-obj-xen := cstate.o
obj-y := $(call filterxen, $(obj-y), $(n-obj-xen))
obj-y := $(call cherrypickxen, $(obj-y), $(src))
endif
