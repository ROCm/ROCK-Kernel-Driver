#
# Makefile for MIPS-specific library files..
#

lib-y	+= csum_partial.o csum_partial_copy.o memcpy.o \
	   memset.o promlib.o rtc-std.o rtc-no.o strlen_user.o \
	   strncpy_user.o strnlen_user.o watch.o 

ifeq ($(CONFIG_CPU_R3000)$(CONFIG_CPU_TX39XX),y)
  lib-y	+= r3k_dump_tlb.o
else
  lib-y	+= dump_tlb.o
endif

lib-$(CONFIG_BLK_DEV_FD)	+= floppy-no.o floppy-std.o
lib-$(subst m,y,$(CONFIG_IDE))	+= ide-std.o ide-no.o   # needed for ide module

EXTRA_AFLAGS := $(CFLAGS)
