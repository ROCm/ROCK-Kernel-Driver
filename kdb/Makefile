#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
#

obj-y		:= kdb_bt.o kdb_bp.o kdb_id.o kdbsupport.o gen-kdb_cmds.o kdbmain.o kdb_io.o

# The i386 and x86_64 backtrace commands are handled by common code.
ifdef CONFIG_X86
  obj-y		+= kdba_bt_x86.o
  ifneq (,$(findstring -fno-optimize-sibling-calls,$(CFLAGS)))
    CFLAGS_kdba_bt_x86.o += -DNO_SIBLINGS
  endif
  REGPARM := $(subst -mregparm=,,$(filter -mregparm=%,$(CFLAGS)))
  ifeq (,$(REGPARM))
    ifdef CONFIG_X86_64
      REGPARM := 6
    else
      REGPARM := 0
    endif
  endif
  CFLAGS_kdba_bt_x86.o += -DREGPARM=$(REGPARM)
endif

subdir-$(CONFIG_KDB_MODULES) := modules
obj-y += $(addsuffix /built-in.o, $(subdir-y))

clean-files := gen-kdb_cmds.c

override CFLAGS := $(CFLAGS:%-pg=% )

quiet_cmd_gen-kdb = GENKDB  $@
      cmd_gen-kdb = $(AWK) 'BEGIN {print "\#include <linux/stddef.h>"; print "\#include <linux/init.h>"} \
		/^\#/{next} \
		/^[ \t]*$$/{next} \
		{gsub(/"/, "\\\"", $$0); \
		  print "static __initdata char kdb_cmd" cmds++ "[] = \"" $$0 "\\n\";"} \
		END {print "extern char *kdb_cmds[]; char __initdata *kdb_cmds[] = {"; for (i = 0; i < cmds; ++i) {print "  kdb_cmd" i ","}; print("  NULL\n};");}' \
		$(filter-out %/Makefile,$^) > $@

$(obj)/gen-kdb_cmds.c:	$(src)/kdb_cmds $(wildcard $(TOPDIR)/arch/$(ARCH)/kdb/kdb_cmds) $(src)/Makefile
	$(call cmd,gen-kdb)
