#
# Makefile for ppc-specific library files..
#

O_TARGET		:= lib.o

export-objs		:= dec_and_lock.o

obj-y			:= checksum.o string.o strcase.o dec_and_lock.o

obj-$(CONFIG_SMP)	+= locks.o

include $(TOPDIR)/Rules.make
