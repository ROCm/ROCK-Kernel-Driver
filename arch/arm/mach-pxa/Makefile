#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).

O_TARGET		:= pxa.o

obj-y :=
obj-m :=
obj-n :=
obj-  :=

export-objs := generic.o irq.o dma.o sa1111.o

# Common support (must be linked before board specific support)
obj-y += generic.o irq.o dma.o
obj-$(CONFIG_SA1111) += sa1111.o

# Specific board support
obj-$(CONFIG_ARCH_LUBBOCK) += lubbock.o
obj-$(CONFIG_ARCH_PXA_IDP) += idp.o

# Support for blinky lights
leds-y := leds.o
leds-$(CONFIG_ARCH_LUBBOCK) += leds-lubbock.o
leds-$(CONFIG_ARCH_PXA_IDP) += leds-idp.o

obj-$(CONFIG_LEDS) += $(leds-y)

include $(TOPDIR)/Rules.make
