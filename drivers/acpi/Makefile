#
# Makefile for the Linux ACPI interpreter
# 

O_TARGET := acpi.o

export ACPI_CFLAGS

ACPI_CFLAGS	:= -D_LINUX -I$(CURDIR)/include

ifdef CONFIG_ACPI_DEBUG
  ACPI_CFLAGS	+= -DACPI_DEBUG -Wno-unused
endif

EXTRA_CFLAGS	+= $(ACPI_CFLAGS)

export-objs 	:= acpi_ksyms.o

obj-y		:= acpi_ksyms.o 

#
# ACPI Boot-Time Table Parsing
#
ifeq ($(CONFIG_ACPI_BOOT),y)
  obj-y		+= acpi_tables.o
endif

#
# ACPI Core Subsystem (Interpreter)
#
ifeq ($(CONFIG_ACPI_INTERPRETER),y)
  obj-y		+= acpi_osl.o acpi_utils.o
  subdir-y	+= dispatcher events executer hardware namespace parser \
		   resources tables utilities
  obj-y 	+= $(foreach dir,$(subdir-y),$(dir)/$(dir).o)
endif

#
# ACPI Bus and Device Drivers
#
ifeq ($(CONFIG_ACPI_BUS),y)
  obj-y				+= acpi_bus.o 
  obj-$(CONFIG_ACPI_AC) 	+= acpi_ac.o
  obj-$(CONFIG_ACPI_BATTERY)	+= acpi_battery.o
  obj-$(CONFIG_ACPI_BUTTON)	+= acpi_button.o
  obj-$(CONFIG_ACPI_EC)		+= acpi_ec.o
  obj-$(CONFIG_ACPI_FAN)	+= acpi_fan.o
  obj-$(CONFIG_ACPI_PCI)	+= acpi_pci_root.o acpi_pci_link.o
  obj-$(CONFIG_ACPI_POWER)	+= acpi_power.o
  obj-$(CONFIG_ACPI_PROCESSOR)	+= acpi_processor.o
  obj-$(CONFIG_ACPI_THERMAL)	+= acpi_thermal.o
  obj-$(CONFIG_ACPI_SYSTEM)	+= acpi_system.o
endif

include $(TOPDIR)/Rules.make
