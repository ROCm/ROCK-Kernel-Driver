#
# Makefile for all Linux ACPI interpreter subdirectories
#

O_TARGET := ../$(shell basename `pwd`).o

obj-$(CONFIG_ACPI) := $(patsubst %.c,%.o,$(wildcard *.c))

EXTRA_CFLAGS += -I../include

EXTRA_CFLAGS += $(ACPI_CFLAGS)

include $(TOPDIR)/Rules.make

clean:
	$(RM) *.o 
