# $Id: Makefile,v 1.8 2000/12/14 22:57:25 davem Exp $
# Makefile for the linux Sparc64-specific parts of the memory manager.
#

EXTRA_AFLAGS := -ansi

obj-y    := ultra.o fault.o init.o generic.o extable.o modutil.o

obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o

include $(TOPDIR)/Rules.make
