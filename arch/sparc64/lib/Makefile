# $Id: Makefile,v 1.25 2000/12/14 22:57:25 davem Exp $
# Makefile for Sparc64 library files..
#

.S.s:
	$(CPP) $(AFLAGS) -ansi $< -o $*.s

.S.o:
	$(CC) $(AFLAGS) -ansi -c $< -o $*.o

CFLAGS := $(CFLAGS)

L_TARGET = lib.a
obj-y := PeeCeeI.o blockops.o debuglocks.o strlen.o strncmp.o \
	 memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	 VIScopy.o VISbzero.o VISmemset.o VIScsum.o VIScsumcopy.o \
	 VIScsumcopyusr.o VISsave.o atomic.o rwlock.o bitops.o \
	 dec_and_lock.o U3memcpy.o U3copy_from_user.o U3copy_to_user.o \
	 U3copy_in_user.o

include $(TOPDIR)/Rules.make
