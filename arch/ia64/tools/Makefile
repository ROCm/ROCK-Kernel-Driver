CFLAGS	= -g -O2 -Wall $(CPPFLAGS)

THIS	= arch/$(ARCH)/tools
TARGET	= $(TOPDIR)/include/asm-ia64/offsets.h

all:

fastdep:

mrproper: clean

clean:
	rm -f $(THIS)/print_offsets.s $(THIS)/print_offsets $(THIS)/offsets.h

$(TARGET): $(THIS)/offsets.h
	echo here we go
	if ! cmp -s $(THIS)/offsets.h ${TARGET}; then	\
		echo -e "*** Updating ${TARGET}...";	\
		cp $(THIS)/offsets.h ${TARGET};		\
	else						\
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

$(THIS)/offsets.h: $(THIS)/print_offsets
	$(THIS)/print_offsets > $(THIS)/offsets.h

comma	:= ,

$(THIS)/print_offsets: $(THIS)/print_offsets.c FORCE
	$(CC) $(CFLAGS) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) \
		$(THIS)/print_offsets.c -o $@

FORCE:

else

$(THIS)/offsets.h: $(THIS)/print_offsets.s
	$(AWK) -f $(THIS)/print_offsets.awk $^ > $@

$(THIS)/print_offsets.s: $(THIS)/print_offsets.c
	$(CC) $(CFLAGS) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) -S $^ -o $@

endif

.PHONY: all modules modules_install
