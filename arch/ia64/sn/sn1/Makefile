#
# ia64/platform/sn/sn1/Makefile
#
# Copyright (C) 1999 Silicon Graphics, Inc.
# Copyright (C) Srinivasa Thirumalachar (sprasad@engr.sgi.com)
#

EXTRA_CFLAGS	:= -DSN -DLANGUAGE_C=1 -D_LANGUAGE_C=1 -I. -DBRINGUP \
		   -DDIRECT_L1_CONSOLE -DNUMA_BASE -DSIMULATED_KLGRAPH \
		   -DNUMA_MIGR_CONTROL -DLITTLE_ENDIAN -DREAL_HARDWARE \
		   -DNEW_INTERRUPTS

.S.s:
	$(CPP) $(AFLAGS) $(AFLAGS_KERNEL) -o $*.s $<
.S.o:
	$(CC) $(AFLAGS) $(AFLAGS_KERNEL) -c -o $*.o $<

all: sn1.a

O_TARGET        = sn1.a

obj-y          = irq.o setup.o iomv.o mm.o smp.o synergy.o sn1_asm.o \
		discontig.o probe.o error.o sv.o

obj-$(CONFIG_IA64_SGI_AUTOTEST)	+= llsc4.o
obj-$(CONFIG_IA64_GENERIC)	+= machvec.o
obj-$(CONFIG_MODULES)		+= sn1_ksyms.o

clean::

include $(TOPDIR)/Rules.make
