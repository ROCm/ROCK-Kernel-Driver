#
# Makefile for the linux kernel.
#

obj-y			:= main.o version.o do_mounts.o initramfs.o
obj-$(CONFIG_MODULES)	+= vermagic.o

# files to be removed upon make clean
clean-files := ../include/linux/compile.h

# dependencies on generated files need to be listed explicitly

$(obj)/version.o: include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

include/linux/compile.h: FORCE
	@echo -n '  Generating $@'
	@sh $(srctree)/scripts/mkcompile_h $@ "$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CC) $(CFLAGS)"
