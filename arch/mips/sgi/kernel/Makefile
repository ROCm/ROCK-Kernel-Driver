#
# Makefile for the SGI specific kernel interface routines
# under Linux.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

O_TARGET := ip22-kern.o

all: ip22-kern.o indyIRQ.o

obj-y	+= indy_mc.o indy_sc.o indy_hpc.o indy_int.o indy_rtc.o system.o \
	   indyIRQ.o reset.o setup.o time.o

indyIRQ.o: indyIRQ.S

include $(TOPDIR)/Rules.make
