#
# Makefile for x86_64-specific library files..
#

USE_STANDARD_AS_RULE := true

L_TARGET = lib.a
obj-y  = generic-checksum.o old-checksum.o delay.o \
	usercopy.o getuser.o putuser.o  \
	checksum_copy.o thunk.o mmx.o

obj-$(CONFIG_IO_DEBUG) += iodebug.o
obj-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o

include $(TOPDIR)/Rules.make
