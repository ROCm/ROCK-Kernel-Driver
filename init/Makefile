#
# Makefile for the linux kernel.
#

O_TARGET := init.o

obj-y    := main.o version.o do_mounts.o

include $(TOPDIR)/Rules.make

# dependencies on generated files need to be listed explicitly

version.o: ../include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actualy file if its content has changed.

../include/linux/compile.h: ../include/linux/version.h dummy
	@echo Generating $@
	@. ../scripts/mkcompile_h $@ "$(ARCH)" "$(CONFIG_SMP)" "$(CC) $(CFLAGS)"
