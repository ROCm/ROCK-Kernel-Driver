#
# Makefile for the Linux OSPM code.
#

O_TARGET := $(notdir $(CURDIR)).o

ACPI_CFLAGS += -I$(CURDIR)/include

EXTRA_CFLAGS += $(ACPI_CFLAGS)

subdir-$(CONFIG_ACPI_BUSMGR)	+= busmgr
subdir-$(CONFIG_ACPI_EC)	+= ec
subdir-$(CONFIG_ACPI_SYS)	+= system
subdir-$(CONFIG_ACPI_CPU)	+= processor
subdir-$(CONFIG_ACPI_CMBATT)	+= battery
subdir-$(CONFIG_ACPI_AC)	+= ac_adapter
subdir-$(CONFIG_ACPI_BUTTON)	+= button
subdir-$(CONFIG_ACPI_THERMAL)	+= thermal

obj-y				+= $(foreach dir,$(subdir-y),$(dir)/ospm_$(dir).o)

include $(TOPDIR)/Rules.make
