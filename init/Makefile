#
# Makefile for the linux kernel.
#

obj-y    := main.o version.o do_mounts.o

# files to be removed upon make clean
clean-files := ../include/linux/compile.h

include $(TOPDIR)/Rules.make

# dependencies on generated files need to be listed explicitly

$(obj)/version.o: $(obj)/../include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

$(obj)/../include/linux/compile.h: FORCE
	@echo -n '  Generating $(echo_target)'
	@$(srctree)/scripts/mkcompile_h $@ "$(ARCH)" "$(CONFIG_SMP)" "$(CC) $(CFLAGS)"

