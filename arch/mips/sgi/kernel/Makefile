#
# Makefile for the SGI specific kernel interface routines
# under Linux.
#

O_TARGET := ip22-kern.o

obj-y	+= indy_mc.o indy_sc.o indy_hpc.o indy_int.o indy_rtc.o system.o \
	   indyIRQ.o reset.o setup.o time.o

EXTRA_AFLAGS := $(CFLAGS)

include $(TOPDIR)/Rules.make
