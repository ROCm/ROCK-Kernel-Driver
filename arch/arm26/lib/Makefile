#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-2000 Russell King
#

L_TARGET	:= lib.a

obj-y		:= backtrace.o changebit.o csumipv6.o csumpartial.o   \
		   csumpartialcopy.o csumpartialcopyuser.o clearbit.o \
		   copy_page.o delay.o findbit.o memchr.o memcpy.o    \
		   memset.o memzero.o setbit.o    \
		   strchr.o strrchr.o testchangebit.o  \
		   testclearbit.o testsetbit.o getuser.o              \
		   putuser.o ashldi3.o ashrdi3.o lshrdi3.o muldi3.o   \
		   ucmpdi2.o udivdi3.o lib1funcs.o ecard.o io-acorn.o \
		   floppydma.o io-readsb.o io-writesb.o io-writesl.o  \
		   uaccess-kernel.o uaccess-user.o io-readsw-armv3.o  \
		   io-writesw-armv3.o io-readsl-armv3.o

obj-m		:=
obj-n		:=

obj-$(CONFIG_VT)+= kbd.o

obj-y		+= ecard.o io-acorn.o floppydma.o


csumpartialcopy.o: csumpartialcopygeneric.S
csumpartialcopyuser.o: csumpartialcopygeneric.S

