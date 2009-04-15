#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
#

obj-$(CONFIG_KDB)	+= kdba_bp.o x86-dis.o kdba_bt.o \
			   kdba_io.o kdba_id.o kdba_support.o

ifneq (,$(findstring -fno-optimize-sibling-calls,$(KBUILD_CFLAGS)))
  CFLAGS_kdba_bt.o += -DNO_SIBLINGS
endif

REGPARM := $(subst -mregparm=,,$(filter -mregparm=%,$(KBUILD_CFLAGS)))
ifeq (,$(REGPARM))
ifeq ($(CONFIG_X86_32),y)
  REGPARM := 3
else
  REGPARM := 6
endif
endif

CFLAGS_kdba_bt.o += -DREGPARM=$(REGPARM) -DCCVERSION="$(CCVERSION)"

override CFLAGS := $(CFLAGS:%-pg=% )

CFLAGS_kdba_io.o += -I $(TOPDIR)/arch/$(SRCARCH)/kdb
