#
# Makefile for the Linux ACPI interpreter
# 

O_TARGET := acpi.o

export-objs := acpi_ksyms.o

export ACPI_CFLAGS

ACPI_CFLAGS := -D_LINUX -I$(CURDIR)/include

#
# CONFIG_ACPI_KERNEL_CONFIG is currently only IA64
#
ifdef CONFIG_ACPI_KERNEL_CONFIG
  ACPI_CFLAGS += -DCONFIG_ACPI_KERNEL_CONFIG_ONLY
endif

acpi-subdirs := utilities dispatcher events hardware \
		executer namespace parser resources tables

ifdef CONFIG_ACPI_DEBUG
  ACPI_CFLAGS += -DACPI_DEBUG -Wno-unused
endif

ifdef CONFIG_ACPI_DEBUGGER
  ACPI_CFLAGS += -DENABLE_DEBUGGER
  acpi-subdirs += debugger
endif

EXTRA_CFLAGS += $(ACPI_CFLAGS)

mod-subdirs  := ospm

subdir-$(CONFIG_ACPI) 		+= $(acpi-subdirs)
subdir-$(CONFIG_ACPI_BUSMGR)	+= ospm

obj-$(CONFIG_ACPI)	+= driver.o os.o acpi_ksyms.o
obj-$(CONFIG_ACPI)	+= $(foreach dir,$(acpi-subdirs),$(dir)/$(dir).o) 
ifdef CONFIG_ACPI_KERNEL_CONFIG
   obj-$(CONFIG_ACPI) 	+= acpiconf.o osconf.o
endif

ifeq ($(CONFIG_ACPI_BUSMGR),y)
	obj-y			+= ospm/ospm.o
endif

# commented out until we distribute it -ASG
#ifeq ($(CONFIG_KDB),y)
#	obj-m	+= kdb/kdbm_acpi.o
#endif

include $(TOPDIR)/Rules.make
