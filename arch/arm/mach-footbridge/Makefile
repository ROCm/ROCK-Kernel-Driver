#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

O_TARGET		:= footbridge.o

# Object file lists.

obj-y			:= arch.o #dma.o mm.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= netwinder-hw.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_ARCH_CATS) += cats-pci.o
obj-$(CONFIG_ARCH_EBSA285) += ebsa285-pci.o
obj-$(CONFIG_ARCH_NETWINDER) += netwinder-pci.o
obj-$(CONFIG_ARCH_PERSONAL_SERVER) += personal-pci.o
endif

ifeq ($(CONFIG_LEDS),y)
obj-$(CONFIG_ARCH_CO285) += ebsa285-leds.o
obj-$(CONFIG_ARCH_EBSA285) += ebsa285-leds.o
obj-$(CONFIG_ARCH_NETWINDER) += netwinder-leds.o
endif

obj-$(CONFIG_ARCH_CATS) += cats-hw.o
obj-$(CONFIG_ARCH_NETWINDER) += netwinder-hw.o

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS 	:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS 	:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
