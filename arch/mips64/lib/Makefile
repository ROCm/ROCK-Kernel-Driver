#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a
L_OBJS	= csum_partial.o csum_partial_copy.o dump_tlb.o floppy-std.o \
	  floppy-no.o ide-std.o ide-no.o kbd-std.o kbd-no.o rtc-std.o \
	  rtc-no.o memset.o memcpy.o strlen_user.o strncpy_user.o \
	  strnlen_user.o watch.o

include $(TOPDIR)/Rules.make
