O_TARGET	:= ospm_$(notdir $(CURDIR)).o
obj-m		:= $(O_TARGET)
EXTRA_CFLAGS	+= $(ACPI_CFLAGS)
obj-y		:= $(patsubst %.c,%.o,$(wildcard *.c))

include $(TOPDIR)/Rules.make
