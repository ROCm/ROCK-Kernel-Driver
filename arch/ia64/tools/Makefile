CFLAGS	= -g -O2 -Wall $(CPPFLAGS)

TARGET	= include/asm-ia64/offsets.h

src = $(obj)

clean-files := print_offsets.s print_offsets offsets.h

$(TARGET): $(obj)/offsets.h
	@if ! cmp -s $(obj)/offsets.h ${TARGET}; then	\
		echo -e "*** Updating ${TARGET}...";	\
		cp $(obj)/offsets.h ${TARGET};		\
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

$(obj)/offsets.h: $(obj)/print_offsets
	$(obj)/print_offsets > $(obj)/offsets.h

comma	:= ,

$(obj)/print_offsets: $(src)/print_offsets.c FORCE
	[ -r $(TARGET) ] || echo "#define IA64_TASK_SIZE 0" > $(TARGET)
	$(CC) $(CFLAGS) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) \
		$(src)/print_offsets.c -o $@

FORCE:

else

$(obj)/offsets.h: $(obj)/print_offsets.s
	$(AWK) -f $(src)/print_offsets.awk $^ > $@

$(obj)/print_offsets.s: $(src)/print_offsets.c
	[ -r $(TARGET) ] || echo "#define IA64_TASK_SIZE 0" > $(TARGET)
	$(CC) $(CFLAGS) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) -S $^ -o $@

endif

.PHONY: all modules modules_install
