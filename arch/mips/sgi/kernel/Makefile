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

OBJS  = indy_mc.o indy_sc.o indy_hpc.o indy_int.o indy_rtc.o \
        system.o indyIRQ.o reset.o setup.o time.o
ifdef CONFIG_SGI_PROM_CONSOLE
OBJS += promcon.o
endif

all: sgikern.a

sgikern.a: $(OBJS)
	$(AR) rcs sgikern.a $(OBJS)
	sync

indyIRQ.o: indyIRQ.S

dep:
	$(CPP) $(CPPFLAGS) -M *.c > .depend

include $(TOPDIR)/Rules.make
