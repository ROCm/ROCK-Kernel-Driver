# $Id: Makefile,v 1.15 2001/02/16 17:50:04 larsv Exp $
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

LD_SCRIPT=$(TOPDIR)/arch/cris/cris.ld

# A bug in ld prevents us from having a (constant-value) symbol in a
# "ORIGIN =" or "LENGTH =" expression.  We fix that by generating a
# linker file with the symbolic part of those expressions evaluated.
#  Unfortunately, there is trouble making vmlinux depend on anything we
# generate here, so we *always* regenerate the final linker script and
# replace the LD macro to get what we want.  Thankfully(?) vmlinux is
# always rebuilt (due to calling make recursively and not knowing if
# anything was rebuilt).
#  The shell script to build in some kind of dependency is really not
# necessary for reasons of speed.  It's there because always
# regenerating stuff (even for incremental linking of subsystems!) is
# even more nauseating.
LD = if [ ! -e $(LD_SCRIPT).tmp -o $(LD_SCRIPT) -nt $(LD_SCRIPT).tmp ]; then \
          sed -e s/@ETRAX_DRAM_VIRTUAL_BASE@/0x$(ETRAX_DRAM_VIRTUAL_BASE)/ \
              -e s/@ETRAX_DRAM_SIZE_M@/$(ETRAX_DRAM_SIZE)/ \
              < $(LD_SCRIPT) > $(LD_SCRIPT).tmp; \
     else true; \
     fi && $(CROSS_COMPILE)ld -mcriself

LINKFLAGS =-qmagic -mcriself -T $(LD_SCRIPT).tmp

# objcopy is used to make binary images from the resulting linked file

OBJCOPY := $(CROSS_COMPILE)objcopy -O binary -R .note -R .comment -S

# normally, gcc on a linux box adds __linux__ but we do it "manually"
# gcc-cris defaults to a.out, we need ELF, so -melf

CFLAGS := $(CFLAGS) -march=v10 -fno-strict-aliasing -pipe -D__linux__

ifdef CONFIG_KGDB
CFLAGS := $(subst -fomit-frame-pointer,,$(CFLAGS)) -g
CFLAGS += -fno-omit-frame-pointer
endif

HEAD := arch/cris/kernel/head.o

SUBDIRS += arch/cris/kernel arch/cris/mm arch/cris/lib arch/cris/drivers arch/cris/boot/rescue
CORE_FILES += arch/cris/kernel/kernel.o arch/cris/mm/mm.o arch/cris/drivers/drivers.o
LIBGCC = $(shell $(CC) $(CFLAGS) -print-file-name=libgcc.a)
LIBS := $(TOPDIR)/arch/cris/lib/lib.a $(LIBS) $(TOPDIR)/arch/cris/lib/lib.a $(LIBGCC)

arch/cris/kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/cris/kernel

arch/cris/mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=arch/cris/mm

MAKEBOOT = $(MAKE) -C arch/$(ARCH)/boot

vmlinux.bin: vmlinux
	$(OBJCOPY) vmlinux vmlinux.bin

timage: vmlinux.bin
	cat vmlinux.bin cramfs.img >timage

simimage: timage
	cp vmlinux.bin simvmlinux.bin

# the following will remake timage without compiling the kernel
# it does of course require that all object files exist...

cramfs:
## cramfs      - Creates a cramfs image
	mkcramfs -b 8192 root cramfs.img
	cat vmlinux.bin cramfs.img >timage

clinux: vmlinux.bin decompress.bin rescue.bin

decompress.bin: dummy
	@make -C arch/cris/boot/compressed decompress.bin

rescue.bin: dummy
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

archdep:
	@$(MAKEBOOT) dep
