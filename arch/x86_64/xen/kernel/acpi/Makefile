i386-obj-y		:= boot.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
c-obj-y			+= processor.o
endif

$(patsubst %.o,$(obj)/%.c,$(c-obj-y) $(c-link)):
	@ln -fsn $(srctree)/arch/x86_64/kernel/acpi/$(notdir $@) $@

$(patsubst %.o,$(obj)/%.c,$(i386-obj-y)):
	@ln -fsn $(srctree)/arch/i386/xen/kernel/acpi/$(notdir $@) $@

obj-y	+= $(c-obj-y) $(i386-obj-y)

clean-files += $(patsubst %.o,%.c,$(c-obj-y) $(c-obj-))
