#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= arch.o dc21285.o dma.o irq.o isa-irq.o mm.o time.o
obj-m			:=
obj-n			:=
obj-			:=

pci-$(CONFIG_ARCH_CATS) += cats-pci.o
pci-$(CONFIG_ARCH_EBSA285) += ebsa285-pci.o
pci-$(CONFIG_ARCH_NETWINDER) += netwinder-pci.o
pci-$(CONFIG_ARCH_PERSONAL_SERVER) += personal-pci.o

leds-$(CONFIG_ARCH_CO285) += ebsa285-leds.o
leds-$(CONFIG_ARCH_EBSA285) += ebsa285-leds.o
leds-$(CONFIG_ARCH_NETWINDER) += netwinder-leds.o

obj-$(CONFIG_ARCH_CATS) += cats-hw.o
obj-$(CONFIG_ARCH_NETWINDER) += netwinder-hw.o

obj-$(CONFIG_PCI)	+=$(pci-y)
obj-$(CONFIG_LEDS)	+=$(leds-y)
