#
# Makefile for all Linux ACPI interpreter subdirectories
#

obj-$(CONFIG_ACPI_INTERPRETER) := $(patsubst %.c,%.o,$(wildcard *.c))

EXTRA_CFLAGS += $(ACPI_CFLAGS)

include $(TOPDIR)/Rules.make
