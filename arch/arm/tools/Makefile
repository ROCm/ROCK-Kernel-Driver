#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

all:	$(TOPDIR)/include/asm-arm/mach-types.h

$(TOPDIR)/include/asm-arm/mach-types.h: mach-types gen-mach-types
	awk -f gen-mach-types mach-types > $@

.PHONY:	all
