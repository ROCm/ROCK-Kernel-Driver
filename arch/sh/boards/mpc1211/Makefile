#
# Makefile for the Interface (CTP/PCI/MPC-SH02) specific parts of the kernel
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y	 := mach.o setup.o io.o rtc.o led.o

obj-$(CONFIG_PCI) += pci.o

