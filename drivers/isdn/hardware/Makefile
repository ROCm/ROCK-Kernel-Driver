# Makefile for the CAPI hardware drivers

# The target object and module list name.

O_TARGET	:= vmlinux-obj.o

# Object files in subdirectories

mod-subdirs				:= avm
subdir-$(CONFIG_CAPI_AVM)		+= avm

obj-y += $(addsuffix /vmlinux-obj.o, $(subdir-y))

# The global Rules.make.

include $(TOPDIR)/Rules.make
