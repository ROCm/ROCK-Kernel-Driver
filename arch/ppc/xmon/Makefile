# BK Id: SCCS/s.Makefile 1.3 05/17/01 18:14:23 cort
#
# Makefile for xmon

O_TARGET	:= x.o

ifeq ($(CONFIG_8xx),y)
obj-y		:= start_8xx.o xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
else
obj-y		:= start.o xmon.o ppc-dis.o ppc-opc.o subr_prf.o setjmp.o
endif

include $(TOPDIR)/Rules.make
