#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a

obj-y				+= csum_partial.o csum_partial_copy.o \
				   rtc-std.o rtc-no.o memcpy.o memset.o \
				   watch.o strlen_user.o strncpy_user.o \
				   strnlen_user.o

ifdef CONFIG_CPU_R3000
  obj-y	+= r3k_dump_tlb.o 
else
  obj-y	+= dump_tlb.o 
endif

obj-$(CONFIG_BLK_DEV_FD)	+= floppy-no.o floppy-std.o
obj-$(CONFIG_IDE)		+= ide-std.o ide-no.o
obj-$(CONFIG_PC_KEYB)		+= kbd-std.o kbd-no.o

include $(TOPDIR)/Rules.make
