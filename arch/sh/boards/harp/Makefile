#
# Makefile for STMicroelectronics board specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y := irq.o setup.o mach.o led.o

obj-$(CONFIG_PCI) += pcidma.o

