# BK Id: SCCS/s.Makefile 1.10 11/08/01 07:57:40 paulus
#
#
# Makefile for ppc-specific library files..
#

USE_STANDARD_AS_RULE	:= true

O_TARGET		:= lib.o

export-objs		:= dec_and_lock.o

obj-y			:= checksum.o string.o strcase.o dec_and_lock.o

obj-$(CONFIG_SMP)	+= locks.o

include $(TOPDIR)/Rules.make
