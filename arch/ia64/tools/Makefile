CFLAGS	= -g -O2 -Wall $(CPPFLAGS)

TARGET	= $(TOPDIR)/include/asm-ia64/offsets.h

all: 

mrproper:

clean:
	rm -f print_offsets.s print_offsets offsets.h

fastdep: offsets.h
	@if ! cmp -s offsets.h ${TARGET}; then	\
		echo -e "*** Updating ${TARGET}...";	\
		cp offsets.h ${TARGET};		\
	else					\
		echo "*** ${TARGET} is up to date";	\
	fi

#
# If we're cross-compiling, we use the cross-compiler to translate
# print_offsets.c into an assembly file and then awk to translate this
# file into offsets.h.  This avoids having to use a simulator to
# generate this file.  This is based on an idea suggested by Asit
# Mallick.  If we're running natively, we can of course just build
# print_offsets and run it. --davidm
#

ifeq ($(CROSS_COMPILE),)

offsets.h: print_offsets
	./print_offsets > offsets.h

print_offsets: print_offsets.c FORCE_RECOMPILE
	$(CC) $(CFLAGS) print_offsets.c -o $@

FORCE_RECOMPILE:

else

offsets.h: print_offsets.s
	$(AWK) -f print_offsets.awk $^ > $@

print_offsets.s: print_offsets.c
	$(CC) $(CFLAGS) -S print_offsets.c -o $@

endif

.PHONY: all modules modules_install
