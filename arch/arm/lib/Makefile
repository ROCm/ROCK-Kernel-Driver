#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-2000 Russell King
#

lib-y		:= backtrace.o changebit.o csumipv6.o csumpartial.o   \
		   csumpartialcopy.o csumpartialcopyuser.o clearbit.o \
		   copy_page.o delay.o findbit.o memchr.o memcpy.o    \
		   memset.o memzero.o setbit.o strncpy_from_user.o    \
		   strnlen_user.o strchr.o strrchr.o testchangebit.o  \
		   testclearbit.o testsetbit.o uaccess.o getuser.o    \
		   putuser.o ashldi3.o ashrdi3.o lshrdi3.o muldi3.o   \
		   ucmpdi2.o udivdi3.o lib1funcs.o div64.o

lib-arc		:= ecard.o io-acorn.o floppydma.o
lib-rpc		:= ecard.o io-acorn.o floppydma.o
lib-clps7500	:= io-acorn.o
lib-l7200     	:= io-acorn.o
lib-shark	:= io-shark.o
lib-edb7211	:= io-acorn.o

lib-y		+= $(lib-$(MACHINE))

ifeq ($(CONFIG_CPU_32v3),y)
  v3		:= y
  v4		:= n
else
  v3		:= n
  v4		:= y
endif

lib-y		+= io-readsb.o io-writesb.o
lib-$(v3)	+= io-readsw-armv3.o io-writesw-armv3.o io-readsl-armv3.o
lib-$(v4)	+= io-readsw-armv4.o io-writesw-armv4.o io-readsl-armv4.o
lib-y		+= io-writesl.o

$(obj)/csumpartialcopy.o:	$(obj)/csumpartialcopygeneric.S
$(obj)/csumpartialcopyuser.o:	$(obj)/csumpartialcopygeneric.S

