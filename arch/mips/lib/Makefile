#
# Makefile for MIPS-specific library files..
#

lib-y	+= csum_partial_copy.o memcpy.o promlib.o rtc-no.o rtc-std.o

lib-$(subst m,y,$(CONFIG_BLK_DEV_FD))	+= floppy-no.o floppy-std.o
lib-$(subst m,y,$(CONFIG_IDE))		+= ide-no.o ide-std.o

EXTRA_AFLAGS := $(CFLAGS)
