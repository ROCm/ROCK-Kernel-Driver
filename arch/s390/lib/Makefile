#
# Makefile for s390-specific library files..
#

ifdef SMP
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c $< -o $*.o
else
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o
endif

L_TARGET = lib.a

obj-y = checksum.o delay.o memset.o misaligned.o strcmp.o strncpy.o uaccess.o
export-objs += misaligned.o

include $(TOPDIR)/Rules.make

