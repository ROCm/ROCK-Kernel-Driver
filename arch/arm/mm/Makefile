#
# Makefile for the linux arm-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

USE_STANDARD_AS_RULE := true

O_TARGET	:= mm.o

# Object file lists.

obj-y		:= init.o extable.o fault-common.o
obj-m		:=
obj-n		:=
obj-		:=
export-objs	:= proc-syms.o discontig.o

ifeq ($(CONFIG_CPU_32),y)
obj-y		+= consistent.o fault-armv.o ioremap.o mm-armv.o
obj-$(CONFIG_MODULES) += proc-syms.o
endif

obj-$(CONFIG_ALIGNMENT_TRAP) += alignment.o
obj-$(CONFIG_DISCONTIGMEM) += discontig.o

# Select the processor-specific files
p-$(CONFIG_CPU_26)	+= proc-arm2,3.o
p-$(CONFIG_CPU_ARM610)	+= proc-arm6,7.o
p-$(CONFIG_CPU_ARM710)	+= proc-arm6,7.o
p-$(CONFIG_CPU_ARM720T)	+= proc-arm720.o armv4t-late-abort.o
p-$(CONFIG_CPU_ARM920T)	+= proc-arm920.o armv4t-early-abort.o
p-$(CONFIG_CPU_ARM922T)	+= proc-arm922.o armv4t-early-abort.o
p-$(CONFIG_CPU_ARM926T)	+= proc-arm926.o armv5ej-early-abort.o
p-$(CONFIG_CPU_ARM1020)	+= proc-arm1020.o armv4t-early-abort.o
p-$(CONFIG_CPU_SA110)	+= proc-sa110.o armv4-early-abort.o
p-$(CONFIG_CPU_SA1100)	+= proc-sa110.o armv4-early-abort.o minicache.o
p-$(CONFIG_CPU_XSCALE)	+= proc-xscale.o armv4t-early-abort.o minicache.o

obj-y		+= $(sort $(p-y))

include $(TOPDIR)/Rules.make

# Special dependencies
$(p-y):	$(TOPDIR)/include/asm-arm/constants.h
