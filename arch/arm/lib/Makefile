#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-1999 Russell King
#

USE_STANDARD_AS_RULE := true

L_TARGET := lib.a
L_OBJS   := changebit.o csumipv6.o csumpartial.o csumpartialcopy.o \
	    csumpartialcopyuser.o clearbit.o copy_page.o findbit.o \
	    memchr.o memcpy.o memset.o memzero.o setbit.o          \
	    strncpy_from_user.o strnlen_user.o strchr.o strrchr.o  \
	    testchangebit.o testclearbit.o testsetbit.o uaccess.o

l-obj-y	:=
l-obj-n	:=

O_TARGET := lib.o
O_OBJS   := backtrace.o delay.o

ifeq ($(CONFIG_ARCH_ACORN),y)
  half := n
  full := y
else
  half := y
  full := n
endif

L_OBJS_arc		:= ecard.o io-acorn.o floppydma.o
L_OBJS_rpc		:= ecard.o io-acorn.o floppydma.o
L_OBJS_clps7500		:= io-acorn.o
L_OBJS_footbridge	:= io-pcio.o
L_OBJS_l7200     	:= io-acorn.o
L_OBJS_nexuspci		:= io-pcio.o
L_OBJS_sa1100		:= io-pcio.o
L_OBJS_shark		:= io-shark.o
L_OBJS_integrator	:= io-pcio.o
L_OBJS_clps711x		:= io-shark.o

l-obj-y			+= io-readsb.o io-writesb.o
l-obj-$(full)		+= io-readsw-armv3.o io-writesw-armv3.o
l-obj-$(half)		+= io-readsw-armv4.o io-writesw-armv4.o
l-obj-y			+= io-readsl.o io-writesl.o

ifeq ($(PROCESSOR),armo)
  L_OBJS += uaccess-armo.o
endif

ifneq ($(MACHINE),ebsa110)
  OX_OBJS += io.o
endif

L_OBJS	+= $(L_OBJS_$(MACHINE)) $(l-obj-y)

include $(TOPDIR)/Rules.make

constants.h: getconsdata.o extractconstants.pl
	$(PERL) extractconstants.pl $(OBJDUMP) > $@

getconsdata.o: getconsdata.c
	$(CC) $(CFLAGS) -c getconsdata.c
