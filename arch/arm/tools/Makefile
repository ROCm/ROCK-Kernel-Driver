#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

include $(TOPDIR)/Rules.make

include/asm-arm/mach-types.h: $(obj)/mach-types $(obj)/gen-mach-types
	@echo '  Generating $@'
	@awk -f $(obj)/gen-mach-types $(obj)/mach-types > $@

