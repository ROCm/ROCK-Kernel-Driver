#
# Makefile for s390-specific library files..
#

L_TARGET = lib.a

EXTRA_AFLAGS := -traditional

obj-y = checksum.o delay.o memset.o misaligned.o strcmp.o strncpy.o uaccess.o
export-objs += misaligned.o

include $(TOPDIR)/Rules.make

