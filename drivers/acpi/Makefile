#
# Makefile for the Linux ACPI interpreter
# 

export ACPI_CFLAGS

ACPI_CFLAGS	:= -D_LINUX -I$(CURDIR)/include

ifdef CONFIG_ACPI_DEBUG
  ACPI_CFLAGS	+= -DACPI_DEBUG
endif

EXTRA_CFLAGS	+= $(ACPI_CFLAGS)

export-objs 	:= acpi_ksyms.o

obj-y		:= acpi_ksyms.o 

#
# ACPI Boot-Time Table Parsing
#
obj-$(CONFIG_ACPI_BOOT)		+= acpi_tables.o

#
# ACPI Core Subsystem (Interpreter)
#
obj-$(CONFIG_ACPI_INTERPRETER)	+= acpi_osl.o acpi_utils.o \
				   dispatcher/ events/ executer/ hardware/ \
				   namespace/ parser/ resources/ tables/ \
				   utilities/

#
# ACPI Bus and Device Drivers
#
obj-$(CONFIG_ACPI_BUS)		+= acpi_bus.o 
obj-$(CONFIG_ACPI_AC) 		+= acpi_ac.o
obj-$(CONFIG_ACPI_BATTERY)	+= acpi_battery.o
obj-$(CONFIG_ACPI_BUTTON)	+= acpi_button.o
obj-$(CONFIG_ACPI_EC)		+= acpi_ec.o
obj-$(CONFIG_ACPI_FAN)		+= acpi_fan.o
obj-$(CONFIG_ACPI_PCI)		+= acpi_pci_root.o acpi_pci_link.o
obj-$(CONFIG_ACPI_POWER)	+= acpi_power.o
obj-$(CONFIG_ACPI_PROCESSOR)	+= acpi_processor.o
obj-$(CONFIG_ACPI_THERMAL)	+= acpi_thermal.o
obj-$(CONFIG_ACPI_SYSTEM)	+= acpi_system.o

include $(TOPDIR)/Rules.make
