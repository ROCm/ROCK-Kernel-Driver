#
# Makefile for ppc-specific library files..
#

.S.o:
	$(CC) $(AFLAGS) -c $< -o $*.o

O_TARGET		:= lib.o

obj-y			:= checksum.o string.o strcase.o

obj-$(CONFIG_SMP)	:= locks.o

include $(TOPDIR)/Rules.make
