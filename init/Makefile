#
# Makefile for the linux kernel.
#

obj-y				:= main.o version.o mounts.o initramfs.o
obj-$(CONFIG_GENERIC_CALIBRATE_DELAY) += calibrate.o

mounts-y			:= do_mounts.o
mounts-$(CONFIG_DEVFS_FS)	+= do_mounts_devfs.o
mounts-$(CONFIG_BLK_DEV_RAM)	+= do_mounts_rd.o
mounts-$(CONFIG_BLK_DEV_INITRD)	+= do_mounts_initrd.o
mounts-$(CONFIG_BLK_DEV_MD)	+= do_mounts_md.o

extra-$(CONFIG_KERNTYPES)	+= kerntypes.o
#For IA64, compile kerntypes in dwarf-2 format.
ifeq ($(CONFIG_IA64),y)
cc-option = $(shell if $(CC) $(CFLAGS) $(1) -S -o /dev/null -xc /dev/null \
             > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)
CFLAGS_kerntypes.o             := -gdwarf-2
CFLAGS_kerntypes.o             += $(call cc-option, -fno-eliminate-unused-debug-types)
else
CFLAGS_kerntypes.o             := -gstabs
endif

# files to be removed upon make clean
clean-files := ../include/linux/compile.h

# dependencies on generated files need to be listed explicitly

$(obj)/version.o $(obj)/kerntypes.o: include/linux/compile.h

# compile.h changes depending on hostname, generation number, etc,
# so we regenerate it always.
# mkcompile_h will make sure to only update the
# actual file if its content has changed.

include/linux/compile.h: FORCE
	@echo '  CHK     $@'
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/mkcompile_h $@ \
	"$(UTS_MACHINE)" "$(CONFIG_SMP)" "$(CONFIG_PREEMPT)" "$(CC) $(CFLAGS)"
