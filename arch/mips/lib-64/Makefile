#
# Makefile for MIPS-specific library files..
#

lib-y	+= csum_partial.o memset.o strlen_user.o strncpy_user.o strnlen_user.o \
	   watch.o 

ifeq ($(CONFIG_CPU_R3000)$(CONFIG_CPU_TX39XX),y)
  lib-y	+= r3k_dump_tlb.o
else
  lib-y	+= dump_tlb.o
endif

EXTRA_AFLAGS := $(CFLAGS)
