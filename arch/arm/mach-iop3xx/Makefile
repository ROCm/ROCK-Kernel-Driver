#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= arch.o mm.o xs80200-irq.o iop310-irq.o \
			   iop310-pci.o
obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_ARCH_IQ80310) += iq80310-pci.o iq80310-irq.o

ifneq ($(CONFIG_XSCALE_PMU_TIMER),y)
obj-y			+= iq80310-time.o
endif

obj-$(CONFIG_IOP3XX_AAU) += aau.o
obj-$(CONFIG_IOP3XX_DMA) += dma.o
obj-$(CONFIG_IOP3XX_MU) += message.o
obj-$(CONFIG_IOP3XX_PMON) += pmon.o
