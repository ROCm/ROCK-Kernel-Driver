#
# Makefile for MIPS-specific library files..
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

L_TARGET = lib.a

L_OBJS	= csum_partial.o csum_partial_copy.o \
          rtc-std.o rtc-no.o memcpy.o memset.o  watch.o\
          strlen_user.o strncpy_user.o strnlen_user.o

ifdef CONFIG_CPU_R3000
  L_OBJS += r3k_dump_tlb.o 
else
  L_OBJS += dump_tlb.o 
endif

ifdef CONFIG_BLK_DEV_FD
  L_OBJS += floppy-no.o floppy-std.o
endif

ifdef CONFIG_IDE
  L_OBJS += ide-std.o ide-no.o
endif

ifdef CONFIG_PC_KEYB
  L_OBJS += kbd-std.o kbd-no.o
endif

include $(TOPDIR)/Rules.make
