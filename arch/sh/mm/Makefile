#
# Makefile for the Linux SuperH-specific parts of the memory manager.
#

obj-y	 := init.o fault.o extable.o clear_page.o copy_page.o

obj-$(CONFIG_CPU_SH3) += cache-sh3.o
obj-$(CONFIG_CPU_SH4) += cache-sh4.o __clear_user_page-sh4.o __copy_user_page-sh4.o ioremap.o

include $(TOPDIR)/Rules.make
