#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= arch.o

obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_ARCH_IOP321)  += iop321-irq.o iop321-pci.o mm-321.o iop321-time.o

obj-$(CONFIG_ARCH_IQ80321) += iq80321-pci.o

obj-$(CONFIG_IOP3XX_AAU) += aau.o
obj-$(CONFIG_IOP3XX_DMA) += dma.o
