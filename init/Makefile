#
# Makefile for the linux kernel.
#

O_TARGET := init.o

obj-y    := main.o version.o do_mounts.o

include $(TOPDIR)/Rules.make

# dependencies on generated files need to be listed explicitly

$(obj)/version.o: $(objtree)/include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

$(objtree)/include/linux/compile.h: FORCE
	@echo -n '  Generating $@'
	@$(srctree)/scripts/mkcompile_h $@ "$(ARCH)" "$(CONFIG_SMP)" "$(CC) $(CFLAGS)"
