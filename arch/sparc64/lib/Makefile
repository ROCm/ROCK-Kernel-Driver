# $Id: Makefile,v 1.25 2000/12/14 22:57:25 davem Exp $
# Makefile for Sparc64 library files..
#

EXTRA_AFLAGS := -ansi
EXTRA_CFLAGS := -Werror

lib-y := PeeCeeI.o blockops.o strlen.o strncmp.o \
	 memscan.o strncpy_from_user.o strlen_user.o memcmp.o checksum.o \
	 VIScopy.o VISbzero.o VISmemset.o VIScsum.o VIScsumcopy.o \
	 VIScsumcopyusr.o VISsave.o atomic.o rwlock.o bitops.o \
	 U3memcpy.o U3copy_from_user.o U3copy_to_user.o \
	 U3copy_in_user.o mcount.o ipcsum.o rwsem.o xor.o

lib-$(CONFIG_DEBUG_SPINLOCK) += debuglocks.o
lib-$(CONFIG_HAVE_DEC_LOCK) += dec_and_lock.o
