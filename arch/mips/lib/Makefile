#
# Makefile for MIPS-specific library files..
#

EXTRA_AFLAGS := $(CFLAGS)

lib-y				+= csum_partial.o csum_partial_copy.o \
				   rtc-std.o rtc-no.o memcpy.o memset.o \
				   watch.o strlen_user.o strncpy_user.o \
				   strnlen_user.o

ifdef CONFIG_CPU_R3000
  lib-y	+= r3k_dump_tlb.o 
else
  lib-y	+= dump_tlb.o 
endif

lib-$(CONFIG_BLK_DEV_FD)	+= floppy-no.o floppy-std.o
lib-$(CONFIG_IDE)		+= ide-std.o ide-no.o
lib-$(CONFIG_PC_KEYB)		+= kbd-std.o kbd-no.o
