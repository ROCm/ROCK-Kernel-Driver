#
# ia64/platform/sn/sn1/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

EXTRA_CFLAGS	:= -DSN -DLANGUAGE_C=1 -D_LANGUAGE_C=1 -I. -DBRINGUP \
		   -DDIRECT_L1_CONSOLE -DNUMA_BASE -DSIMULATED_KLGRAPH \
		   -DNUMA_MIGR_CONTROL -DLITTLE_ENDIAN -DREAL_HARDWARE \
		   -DNEW_INTERRUPTS -DCONFIG_IA64_SGI_IO

.S.s:
	$(CPP) $(AFLAGS) $(AFLAGS_KERNEL) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) $(AFLAGS_KERNEL) -c -o $*.o $<

all: sn1.a

O_TARGET        = sn1.a
O_HEADERS       =
O_OBJS          = irq.o setup.o iomv.o mm.o smp.o synergy.o sn1_asm.o \
		discontig.o

ifeq ($(CONFIG_IA64_SGI_AUTOTEST),y)
O_OBJS          += llsc4.o
endif


ifeq ($(CONFIG_IA64_GENERIC),y)
O_OBJS		+= machvec.o
endif

clean::

include $(TOPDIR)/Rules.make
