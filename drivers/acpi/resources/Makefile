#
# Makefile for all Linux ACPI interpreter subdirectories
# EXCEPT for the ospm directory
#

O_TARGET := $(notdir $(CURDIR)).o

obj-$(CONFIG_ACPI) := $(patsubst %.c,%.o,$(wildcard *.c))

EXTRA_CFLAGS += $(ACPI_CFLAGS)

include $(TOPDIR)/Rules.make
