# BK Id: SCCS/s.Makefile 1.7 05/17/01 18:14:22 cort
#
#
# Makefile for ppc-specific library files..
#

USE_STANDARD_AS_RULE	:= true

O_TARGET		:= lib.o

obj-y			:= checksum.o string.o strcase.o

obj-$(CONFIG_SMP)	+= locks.o

include $(TOPDIR)/Rules.make
