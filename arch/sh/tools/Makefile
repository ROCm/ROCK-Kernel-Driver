#
# arch/sh/tools/Makefile
#
# Copyright (C) 2003  Paul Mundt
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Shamelessly cloned from ARM.
#

include/asm-sh/machtypes.h: $(obj)/machgen.sh $(obj)/mach-types
	@echo '  Generating $@'
	@$(CONFIG_SHELL) $(obj)/machgen.sh $(obj)/mach-types > $@

