#
# Makefile for the Intel(R) PRO/1000 ethernet driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := e1000.o

obj-y   := e1000_main.o e1000_hw.o e1000_ethtool.o e1000_param.o e1000_proc.o
obj-m   := $(O_TARGET)

include $(TOPDIR)/Rules.make
