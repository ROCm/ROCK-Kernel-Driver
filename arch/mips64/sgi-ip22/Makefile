#
# Makefile for the SGI specific kernel interface routines
# under Linux.
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = ip22.a

obj-y	+= ip22-berr.o ip22-mc.o ip22-sc.o ip22-hpc.o ip22-int.o ip22-rtc.o \
	   ip22-setup.o system.o ip22-timer.o ip22-irq.o ip22-reset.o time.o

include $(TOPDIR)/Rules.make
