#
# Makefile for the linux i386-specific parts of the memory manager.
#

export-objs := pageattr.o

obj-y	:= init.o pgtable.o fault.o ioremap.o extable.o pageattr.o 
obj-$(CONFIG_DISCONTIGMEM)	+= discontig.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o

include $(TOPDIR)/Rules.make
