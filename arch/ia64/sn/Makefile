#
# ia64/sn/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

EXTRA_CFLAGS	:= -DSN -DLANGUAGE_C=1 -D_LANGUAGE_C=1 -I. -DBRINGUP \
		   -DDIRECT_L1_CONSOLE -DNUMA_BASE -DSIMULATED_KLGRAPH \
		   -DNUMA_MIGR_CONTROL -DLITTLE_ENDIAN -DREAL_HARDWARE \
		   -DNEW_INTERRUPTS -DCONFIG_IA64_SGI_IO
all: sn.a

O_TARGET        = sn.a
O_HEADERS       =
O_OBJS          = sn1/sn1.a

clean::

include $(TOPDIR)/Rules.make
