#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

O_TARGET		:= shark.o

# Object file lists.

obj-y			:= arch.o dma.o mm.o pci.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:= 

#obj-$(CONFIG_LEDS)	+= leds.o

# Files that are both resident and modular; remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS 	:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS 	:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
