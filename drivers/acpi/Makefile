#
# Makefile for the Linux ACPI interpreter
#

O_TARGET := acpi.o

export-objs := acpi_ksyms.o

export ACPI_CFLAGS
ACPI_CFLAGS := -D_LINUX

#
# CONFIG_ACPI_KERNEL_CONFIG is currently only IA64
#
ifdef CONFIG_ACPI_KERNEL_CONFIG
  ACPI_CFLAGS += -DCONFIG_ACPI_KERNEL_CONFIG_ONLY
endif

EXTRA_CFLAGS += -I./include

EXTRA_CFLAGS += $(ACPI_CFLAGS)

# genksyms only reads $(CFLAGS), it should really read $(EXTRA_CFLAGS) as well.
# Without EXTRA_CFLAGS the gcc pass for genksyms fails, resulting in an empty
# include/linux/modules/acpi_ksyms.ver.  Changing genkyms to use EXTRA_CFLAGS
# will hit everything, too risky in 2.4.0-prerelease.  Bandaid by tweaking
# CFLAGS only for .ver targets.  Review after 2.4.0 release.  KAO

$(MODINCL)/%.ver: CFLAGS := -I./include $(CFLAGS)

acpi-subdirs := common dispatcher events hardware \
		interpreter namespace parser resources tables

subdir-$(CONFIG_ACPI) += $(acpi-subdirs)

obj-$(CONFIG_ACPI) := $(patsubst %,%.o,$(acpi-subdirs))
obj-$(CONFIG_ACPI) += os.o acpi_ksyms.o

ifdef CONFIG_ACPI_KERNEL_CONFIG
  obj-$(CONFIG_ACPI) += acpiconf.o osconf.o
else
  obj-$(CONFIG_ACPI) += driver.o cmbatt.o cpu.o ec.o acpi_ksyms.o sys.o table.o power.o
endif

include $(TOPDIR)/Rules.make

clean:
	$(RM) *.o */*.o
