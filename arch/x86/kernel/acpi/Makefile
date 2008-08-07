subdir-				:= realmode

obj-$(CONFIG_ACPI)		+= boot.o
obj-$(CONFIG_ACPI_SLEEP)	+= sleep.o wakeup_rm.o wakeup_$(BITS).o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= cstate.o processor.o
ifneq ($(CONFIG_PROCESSOR_EXTERNAL_CONTROL),)
obj-$(CONFIG_XEN)		+= processor_extcntl_xen.o
endif
endif

$(obj)/wakeup_rm.o:    $(obj)/realmode/wakeup.bin

$(obj)/realmode/wakeup.bin: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/realmode

disabled-obj-$(CONFIG_XEN)	:= cstate.o wakeup_%.o
