# $Id: Makefile,v 1.3 2002/01/21 15:21:23 bjornw Exp $
# cris/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.

# A bug in ld prevents us from having a (constant-value) symbol in a
# "ORIGIN =" or "LENGTH =" expression.

LD = $(CROSS_COMPILE)ld -mcrislinux

# objcopy is used to make binary images from the resulting linked file

OBJCOPYFLAGS := -O binary -R .note -R .comment -S

# -mlinux enables -march=v10, -fno-underscores, -D__linux__ among others

CFLAGS := $(CFLAGS) -mlinux -pipe

ifdef CONFIG_ETRAX_KGDB
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS)) -g
CFLAGS += -fno-omit-frame-pointer
endif

AFLAGS += -mlinux

HEAD := arch/cris/kernel/head.o

ifdef CONFIG_ETRAX_AXISFLASHMAP
# only build this if axis flash map is used, because they depend on
# each others config options
SUBDIRS += arch/cris/boot/rescue
endif
LIBGCC = $(shell $(CC) $(CFLAGS) -print-file-name=libgcc.a)

core-y		+= arch/cris/kernel/   arch/cris/mm/
drivers-y	+= arch/cris/drivers/
libs-y		+= arch/cris/lib/lib.a $(LIBGCC)

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux.bin: vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) vmlinux vmlinux.bin

timage: vmlinux.bin
	cat vmlinux.bin cramfs.img >timage

simimage: timage
	cp vmlinux.bin simvmlinux.bin

# the following will remake timage without compiling the kernel
# it does of course require that all object files exist...

cramfs:
## cramfs      - Creates a cramfs image
	mkcramfs -b 8192 -m romfs_meta.txt root cramfs.img
	cat vmlinux.bin cramfs.img >timage

clinux: vmlinux.bin decompress.bin rescue.bin

decompress.bin: FORCE
	@make -C arch/cris/boot/compressed decompress.bin

rescue.bin: FORCE
	@make -C arch/cris/boot/rescue rescue.bin

zImage: vmlinux.bin
## zImage     - Compressed kernel (gzip)
	@$(MAKEBOOT) zImage

compressed: zImage

archclean:
	@$(MAKEBOOT) clean
	rm -f timage vmlinux.bin cramfs.img
	rm -rf $(LD_SCRIPT).tmp

archmrproper:
