#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
#

CCVERSION	:= $(shell $(CC) -v 2>&1 | sed -ne '$$p')
obj-y		:= kdb_bt.o kdb_bp.o kdb_id.o kdbsupport.o gen-kdb_cmds.o kdbmain.o kdb_io.o
CFLAGS_kdbmain.o += -DCCVERSION="$(CCVERSION)"

subdir-$(CONFIG_KDB_MODULES) := modules
obj-y += $(addsuffix /built-in.o, $(subdir-y))

clean-files := gen-kdb_cmds.c

override CFLAGS := $(CFLAGS:%-pg=% )

# define architecture dependent kdb_cmds
ifeq ($(CONFIG_IA64),y)
	KDB_CMDS = ia64/kdb/kdb_cmds
else
	ifeq ($(CONFIG_X86_64),y)
		KDB_CMDS = x86/kdb/kdb_cmds_64
	else
		ifeq ($(CONFIG_X86_32),y)
			KDB_CMDS = x86/kdb/kdb_cmds_32
		endif
	endif
endif

quiet_cmd_gen-kdb = GENKDB  $@
      cmd_gen-kdb = $(AWK) 'BEGIN {print "\#include <linux/stddef.h>"; print "\#include <linux/init.h>"} \
		/^\#/{next} \
		/^[ \t]*$$/{next} \
		{gsub(/"/, "\\\"", $$0); \
		  print "static __initdata char kdb_cmd" cmds++ "[] = \"" $$0 "\\n\";"} \
		END {print "extern char *kdb_cmds[]; char __initdata *kdb_cmds[] = {"; for (i = 0; i < cmds; ++i) {print "  kdb_cmd" i ","}; print("  NULL\n};");}' \
		$(filter-out %/Makefile,$^) > $@

$(obj)/gen-kdb_cmds.c:	$(src)/kdb_cmds $(wildcard $(TOPDIR)/arch/$(KDB_CMDS)) $(src)/Makefile
	$(call cmd,gen-kdb)
