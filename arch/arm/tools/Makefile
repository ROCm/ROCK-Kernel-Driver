#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

all:	$(TOPDIR)/include/asm-arm/mach-types.h \
	$(TOPDIR)/include/asm-arm/constants.h

$(TOPDIR)/include/asm-arm/mach-types.h: mach-types gen-mach-types
	awk -f gen-mach-types mach-types > $@

# Generate the constants.h header file using the compiler.  We get
# the compiler to spit out assembly code, and then mundge it into
# what we want.  We do this in several stages so make picks up on
# any errors that occur along the way.

$(TOPDIR)/include/asm-arm/constants.h: constants-hdr getconstants.c
	$(CC) $(CFLAGS) -S -o - getconstants.c > $@.tmp.1
	sed 's/^\(#define .* \)[#$$]\(.*\)/\1\2/;/^#define/!d' $@.tmp.1 > $@.tmp.2
	cat constants-hdr $@.tmp.2 > $@.tmp
	cmp $@.tmp $@ >/dev/null 2>&1 || mv $@.tmp $@
	$(RM) $@.tmp*

# Build our dependencies, and then generate the constants and
# mach-types header files.  If we do it now, mkdep will pick
# the dependencies up later on when it runs through the other
# directories

dep:
	$(TOPDIR)/scripts/mkdep $(CFLAGS) $(EXTRA_CFLAGS) -- getconstants.c |\
	 sed s,getconstants.o,$(TOPDIR)/include/asm-arm/constants.h, > .depend
	$(MAKE) all

.PHONY:	all dep

ifneq ($(wildcard .depend),)
include .depend
endif
