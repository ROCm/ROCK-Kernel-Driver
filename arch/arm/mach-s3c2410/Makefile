#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= s3c2410.o irq.o
obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_ARCH_BAST)    += mach-bast.o
obj-$(CONFIG_MACH_H1940)   += mach-h1940.o
obj-$(CONFIG_ARCH_H1940)   += mach-h1940.o
obj-$(CONFIG_ARCH_SMDK2410) += mach-smdk2410.o
obj-$(CONFIG_MACH_VR1000)  += mach-vr1000.o

#obj-$(CONFIG_PCI)	+=$(pci-y)
#obj-$(CONFIG_LEDS)	+=$(leds-y)
