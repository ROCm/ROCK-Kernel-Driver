#
# Makefile for the linux ppc-specific parts of the memory manager.
#

EXTRA_CFLAGS += -mno-minimal-toc

obj-y := fault.o init.o extable.o imalloc.o
obj-$(CONFIG_DISCONTIGMEM) += numa.o
obj-$(CONFIG_HUGETLB_PAGE) += hugetlbpage.o
