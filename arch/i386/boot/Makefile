#
# arch/i386/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
#

# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, CURRENT, /dev/xxxx or empty, in which case
# the default of FLOPPY is used by 'build'.

ROOT_DEV := CURRENT

# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.

SVGA_MODE := -DSVGA_MODE=NORMAL_VGA

# If you want the RAM disk device, define this to be the size in blocks.

#RAMDISK := -DRAMDISK=512

EXTRA_TARGETS	:= vmlinux.bin bvmlinux.bin bootsect.o bbootsect.o \
		   setup.o bsetup.o

host-progs	:= tools/build

include $(TOPDIR)/Rules.make

# ---------------------------------------------------------------------------

zImage: bootsect setup vmlinux.bin tools/build
	tools/build bootsect setup vmlinux.bin $(ROOT_DEV) > $@

bzImage: bbootsect bsetup bvmlinux.bin tools/build
	tools/build -b bbootsect bsetup bvmlinux.bin $(ROOT_DEV) > $@

vmlinux.bin: compressed/vmlinux FORCE
	$(call if_changed,objcopy)

bvmlinux.bin: compressed/bvmlinux FORCE
	$(call if_changed,objcopy)

compressed/vmlinux: FORCE
	@$(MAKE) -C compressed vmlinux

compressed/bvmlinux: FORCE
	@$(MAKE) -C compressed bvmlinux

zdisk: $(BOOTIMAGE)
	dd bs=8192 if=$(BOOTIMAGE) of=/dev/fd0

zlilo: $(BOOTIMAGE)
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(BOOTIMAGE) > $(INSTALL_PATH)/vmlinuz
	cp $(TOPDIR)/System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install: $(BOOTIMAGE)
	sh -x ./install.sh $(KERNELRELEASE) $(BOOTIMAGE) $(TOPDIR)/System.map "$(INSTALL_PATH)"

EXTRA_AFLAGS		:= -traditional $(SVGA_MODE) $(RAMDISK)
AFLAGS_bbootsect.o	:= -D__BIG_KERNEL__
AFLAGS_bsetup.o	  	:= -D__BIG_KERNEL__

bootsect: bootsect.o
	$(LD) $(LDFLAGS) -Ttext 0x0 -s --oformat binary -o $@ $<

bbootsect: bbootsect.o
	$(LD) $(LDFLAGS) -Ttext 0x0 -s --oformat binary $< -o $@

setup: setup.o
	$(LD) $(LDFLAGS) -Ttext 0x0 -s --oformat binary -e begtext -o $@ $<

bsetup: bsetup.o
	$(LD) $(LDFLAGS) -Ttext 0x0 -s --oformat binary -e begtext -o $@ $<

b%.S: %.S
	cp $< $@

clean:
	@echo 'Cleaning up (boot)'
	@rm -f tools/build
	@rm -f setup bootsect zImage compressed/vmlinux.out
	@rm -f bsetup bbootsect bzImage compressed/bvmlinux.out
	@$(MAKE) -C compressed clean
