#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

arch/arm/include/asm/mach-types.h: $(src)/gen-mach-types $(src)/mach-types
	@echo '  Generating $@'
	@mkdir -p $(dir $@)
	$(Q)$(AWK) -f $^ > $@ || { rm -f $@; /bin/false; }
