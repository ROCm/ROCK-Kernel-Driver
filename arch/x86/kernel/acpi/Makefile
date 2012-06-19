obj-$(CONFIG_ACPI)		+= boot.o
obj-$(CONFIG_ACPI_SLEEP)	+= sleep.o wakeup_$(BITS).o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= cstate.o
ifneq ($(CONFIG_PROCESSOR_EXTERNAL_CONTROL),)
obj-$(CONFIG_XEN)		+= processor_extcntl_xen.o
endif
endif

disabled-obj-$(CONFIG_XEN)	:= cstate.o sleep.o wakeup_%.o
