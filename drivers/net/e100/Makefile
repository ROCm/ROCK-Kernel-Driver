#
# Makefile for the Intel's E100 ethernet driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := e100.o

obj-y   := e100_main.o e100_config.o e100_proc.o e100_phy.o \
	   e100_eeprom.o e100_test.o
obj-m   := $(O_TARGET)

include $(TOPDIR)/Rules.make
