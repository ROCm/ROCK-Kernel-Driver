# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras
# Simplified for ppc64 by Todd Inglett
#
# NOTE:	this code is built for 32 bit in ELF32 format even though
#	it packages a 64 bit kernel.  We do this to simplify the
#	bootloader and increase compatibility with OpenFirmware.
#
#	To this end we need to define BOOTCC, etc, as the tools
#	needed to build the 32 bit image.  These are normally HOSTCC,
#	but may be a third compiler if, for example, you are cross
#	compiling from an intel box.  Once the 64bit ppc gcc is
#	stable it will probably simply be a compiler switch to
#	compile for 32bit mode.
#	To make it easier to setup a cross compiler,
#	CROSS32_COMPILE is setup as a prefix just like CROSS_COMPILE
#	in the toplevel makefile.

CROSS32_COMPILE =
#CROSS32_COMPILE = /usr/local/ppc/bin/powerpc-linux-

BOOTCC		:= $(CROSS32_COMPILE)gcc
BOOTCFLAGS	:= $(HOSTCFLAGS) -Iinclude
BOOTLD		:= $(CROSS32_COMPILE)ld
BOOTAS		:= $(CROSS32_COMPILE)as
BOOTAFLAGS	:= -D__ASSEMBLY__ $(HOSTCFLAGS)

CFLAGS		:= $(CPPFLAGS) -O -fno-builtin -DSTDC_HEADERS
LD_ARGS		:= -Ttext 0x00400000 -e _start
OBJCOPYFLAGS	:= -S -O binary

obj-boot := start.o main.o zlib.o imagesize.o no_initrd.o
OBJS     := crt0.o start.o main.o zlib.o imagesize.o image.o
obj-boot := $(addprefix $(obj)/,$(obj-boot))
OBJS     := $(addprefix $(obj)/,$(OBJS))
targets  += $(obj-boot) $(addprefix $(obj)/,image.c image.o)

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<
$(obj-boot): %.o: %.c FORCE
	$(call if_changed_dep,bootcc)

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -traditional \
						-c -o $@ $<
$(obj)/crt0.o: %.o: %.S FORCE
	$(call if_changed_dep,bootas)

host-progs		:= piggyback addnote addSystemMap addRamDisk
HOSTCFLAGS_piggyback.o	:= -DKERNELBASE=$(KERNELBASE)
EXTRA_TARGETS 		+= zImage zImage.initrd vmlinux.bin vmlinux.gz \
			   vmlinux.sm vmlinux.initrd vmlinux.sminitrd \
			   sysmap.o initrd.o

quiet_cmd_sysmap = SYSMAP   $@
      cmd_sysmap = $(obj)/addSystemMap System.map $< $@
$(obj)/vmlinux.sm: vmlinux $(obj)/addSystemMap System.map FORCE
	$(call if_changed,sysmap)

quiet_cmd_ramdisk = RAMDISK $@
      cmd_ramdisk = $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map $< $@
$(obj)/vmlinux.initrd: vmlinux $(obj)/addRamDisk $(obj)/ramdisk.image.gz System.map FORCE
	$(call if_changed,ramdisk)

$(obj)/vmlinux.sminitrd: $(obj)/vmlinux.sm $(obj)/addRamDisk $(obj)/ramdisk.image.gz FORCE
	$(call if_changed,ramdisk)


quiet_cmd_piggy = PIGGY   $@
      cmd_piggy = $(obj)/piggyback $(@:.o=) < $< | $(BOOTAS) -o $@

$(obj)/image.o: $(obj)/vmlinux.gz $(obj)/piggyback FORCE
	$(call if_changed,piggy)

$(obj)/sysmap.o: System.map $(obj)/piggyback FORCE
	$(call if_changed,piggy)

$(obj)/initrd.o: $(obj)/ramdisk.image.gz $(obj)/piggyback FORCE
	$(call if_changed,piggy)

quiet_cmd_addnote = ADDNOTE $@ 
      cmd_addnote = $(BOOTLD) $(LD_ARGS) -T $(obj)/zImage.lds -o $@ $(OBJS) $<\
		    && $(obj)/addnote $@

$(obj)/zImage: $(obj)/no_initrd.o $(OBJS) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/zImage.initrd: $(obj)/initrd.o $(OBJS) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinux.gz: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

$(obj)/imagesize.c: vmlinux
	@echo Generating $@
	ls -l vmlinux | \
	awk '{printf "/* generated -- do not edit! */\n" \
		"int uncompressed_size = %d;\n", $$5}' > $(obj)/imagesize.c
	$(CROSS_COMPILE)nm -n vmlinux | tail -1 | \
	awk '{printf "long vmlinux_end = 0x%s;\n", substr($$1,8)}' \
		>> $(obj)/imagesize.c

clean-files :=  $(targets)
