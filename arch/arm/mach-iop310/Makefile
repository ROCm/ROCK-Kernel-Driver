#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

USE_STANDARD_AS_RULE := true

O_TARGET		:= iop310.o

# Object file lists.

obj-y			:= arch.o mm.o xs80200-irq.o iop310-irq.o \
			   iop310-pci.o
obj-m			:=
obj-n			:=
obj-			:=

export-objs		:=

obj-$(CONFIG_ARCH_IQ80310) += iq80310-pci.o iq80310-irq.o

ifneq ($(CONFIG_XSCALE_PMU_TIMER),y)
obj-y			+= iq80310-time.o
endif

obj-$(CONFIG_IOP310_AAU) += aau.o
obj-$(CONFIG_IOP310_DMA) += dma.o
obj-$(CONFIG_IOP310_MU) += message.o
obj-$(CONFIG_IOP310_PMON) += pmon.o

include $(TOPDIR)/Rules.make
