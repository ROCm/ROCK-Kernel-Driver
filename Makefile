VERSION = 2
PATCHLEVEL = 5
SUBLEVEL = 16
EXTRAVERSION =

KERNELRELEASE=$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)
KERNELPATH=kernel-$(shell echo $(KERNELRELEASE) | sed -e "s/-//g")

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(CURDIR)

HPATH   	= $(TOPDIR)/include
FINDHPATH	= $(HPATH)/asm $(HPATH)/linux $(HPATH)/scsi $(HPATH)/net

HOSTCC  	= gcc
HOSTCFLAGS	= -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer

CROSS_COMPILE 	=

#
# Include the make variables (CC, etc...)
#

AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
MAKEFILES	= $(TOPDIR)/.config
GENKSYMS	= /sbin/genksyms
DEPMOD		= /sbin/depmod
MODFLAGS	= -DMODULE
CFLAGS_KERNEL	=
AFLAGS_KERNEL	=
PERL		= perl

export	VERSION PATCHLEVEL SUBLEVEL EXTRAVERSION KERNELRELEASE ARCH \
	CONFIG_SHELL TOPDIR HPATH HOSTCC HOSTCFLAGS CROSS_COMPILE AS LD CC \
	CPP AR NM STRIP OBJCOPY OBJDUMP MAKE MAKEFILES GENKSYMS MODFLAGS PERL

all:	do-it-all

#
# Make "config" the default target if there is no configuration file or
# "depend" the target if there is no top-level dependency information.
#

ifeq (.config,$(wildcard .config))
include .config
ifeq (.hdepend,$(wildcard .hdepend))
do-it-all:	vmlinux
else
CONFIGURATION = depend
do-it-all:	depend
endif
else
CONFIGURATION = config
do-it-all:	config
endif

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images.  Uncomment if you want to place them anywhere other than root.
#

#export	INSTALL_PATH=/boot

#
# INSTALL_MOD_PATH specifies a prefix to MODLIB for module directory
# relocations required by build roots.  This is not defined in the
# makefile but the arguement can be passed to make if needed.
#

MODLIB	:= $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)
export MODLIB

#
# standard CFLAGS
#

CPPFLAGS := -D__KERNEL__ -I$(HPATH)

CFLAGS := $(CPPFLAGS) -Wall -Wstrict-prototypes -Wno-trigraphs -O2 \
	  -fomit-frame-pointer -fno-strict-aliasing -fno-common
AFLAGS := -D__ASSEMBLY__ $(CPPFLAGS)

INIT		=init/init.o
CORE_FILES	=kernel/kernel.o mm/mm.o fs/fs.o ipc/ipc.o
NETWORKS	=net/network.o

LIBS		=$(TOPDIR)/lib/lib.a
SUBDIRS		=init kernel lib drivers mm fs net ipc sound

DRIVERS-n :=
DRIVERS-y :=
DRIVERS-m :=
DRIVERS-  :=

DRIVERS-$(CONFIG_ACPI) += drivers/acpi/acpi.o
DRIVERS-$(CONFIG_PCI) += drivers/pci/driver.o
DRIVERS-$(CONFIG_PARPORT) += drivers/parport/driver.o
DRIVERS-y += drivers/base/base.o \
	drivers/char/char.o \
	drivers/block/block.o \
	drivers/misc/misc.o \
	drivers/net/net.o \
	drivers/media/media.o
DRIVERS-$(CONFIG_NUBUS) += drivers/nubus/nubus.a
DRIVERS-$(CONFIG_ATM) += drivers/atm/atm.o
DRIVERS-$(CONFIG_IDE) += drivers/ide/idedriver.o
DRIVERS-$(CONFIG_FC4) += drivers/fc4/fc4.a
DRIVERS-$(CONFIG_SCSI) += drivers/scsi/scsidrv.o
DRIVERS-$(CONFIG_FUSION) += drivers/message/message.o
DRIVERS-$(CONFIG_IEEE1394) += drivers/ieee1394/ieee1394drv.o

ifneq ($(CONFIG_CD_NO_IDESCSI)$(CONFIG_BLK_DEV_IDECD)$(CONFIG_BLK_DEV_SR)$(CONFIG_PARIDE_PCD),)
DRIVERS-y += drivers/cdrom/driver.o
endif

DRIVERS-$(CONFIG_SOUND) += sound/sound.o
DRIVERS-$(CONFIG_MTD) += drivers/mtd/mtdlink.o
DRIVERS-$(CONFIG_PCMCIA) += drivers/pcmcia/pcmcia.o
DRIVERS-$(CONFIG_DIO) += drivers/dio/dio.a
DRIVERS-$(CONFIG_SBUS) += drivers/sbus/sbus_all.o
DRIVERS-$(CONFIG_ZORRO) += drivers/zorro/driver.o
DRIVERS-$(CONFIG_ALL_PPC) += drivers/macintosh/macintosh.o
DRIVERS-$(CONFIG_MAC) += drivers/macintosh/macintosh.o
DRIVERS-$(CONFIG_PNP) += drivers/pnp/pnp.o
DRIVERS-$(CONFIG_SGI_IP22) += drivers/sgi/sgi.a
DRIVERS-$(CONFIG_VT) += drivers/video/video.o
DRIVERS-$(CONFIG_PARIDE) += drivers/block/paride/paride.a
DRIVERS-$(CONFIG_TC) += drivers/tc/tc.a
DRIVERS-$(CONFIG_USB) += drivers/usb/usbdrv.o
DRIVERS-$(CONFIG_INPUT) += drivers/input/inputdrv.o
DRIVERS-$(CONFIG_GAMEPORT) += drivers/input/gameport/gamedrv.o
DRIVERS-$(CONFIG_SERIO) += drivers/input/serio/seriodrv.o
DRIVERS-$(CONFIG_I2O) += drivers/message/message.o
DRIVERS-$(CONFIG_I2C) += drivers/i2c/i2c.o
DRIVERS-$(CONFIG_PHONE) += drivers/telephony/telephony.o
DRIVERS-$(CONFIG_MD) += drivers/md/mddev.o
DRIVERS-$(CONFIG_BLUEZ) += drivers/bluetooth/bluetooth.o
DRIVERS-$(CONFIG_HOTPLUG_PCI) += drivers/hotplug/vmlinux-obj.o
DRIVERS-$(CONFIG_ISDN) += drivers/isdn/vmlinux-obj.o

DRIVERS := $(DRIVERS-y)


include arch/$(ARCH)/Makefile

export	CPPFLAGS CFLAGS CFLAGS_KERNEL AFLAGS AFLAGS_KERNEL

export	NETWORKS DRIVERS LIBS HEAD LDFLAGS LINKFLAGS MAKEBOOT ASFLAGS

# Build vmlinux / boot target
# ---------------------------------------------------------------------------

boot: vmlinux
	@$(MAKE) CFLAGS="$(CFLAGS) $(CFLAGS_KERNEL)" AFLAGS="$(AFLAGS) $(AFLAGS_KERNEL)" -C arch/$(ARCH)/boot

vmlinux: include/linux/version.h $(CONFIGURATION) linuxsubdirs
	$(LD) $(LINKFLAGS) $(HEAD) $(INIT) \
		--start-group \
		$(CORE_FILES) \
		$(LIBS) \
		$(DRIVERS) \
		$(NETWORKS) \
		--end-group \
		-o vmlinux
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aUw] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > System.map

# 	Handle descending into subdirectories listed in $(SUBDIRS)

linuxsubdirs: $(patsubst %, _dir_%, $(SUBDIRS))

$(patsubst %, _dir_%, $(SUBDIRS)) : dummy include/linux/version.h include/config/MARKER
	@$(MAKE) CFLAGS="$(CFLAGS) $(CFLAGS_KERNEL)" AFLAGS="$(AFLAGS) $(AFLAGS_KERNEL)" -C $(patsubst _dir_%, %, $@)

# Configuration
# ---------------------------------------------------------------------------

oldconfig: symlinks
	$(CONFIG_SHELL) scripts/Configure -d arch/$(ARCH)/config.in

xconfig: symlinks
	@$(MAKE) -C scripts kconfig.tk
	wish -f scripts/kconfig.tk

menuconfig: include/linux/version.h symlinks
	@$(MAKE) -C scripts/lxdialog all
	$(CONFIG_SHELL) scripts/Menuconfig arch/$(ARCH)/config.in

config: symlinks
	$(CONFIG_SHELL) scripts/Configure arch/$(ARCH)/config.in

# make asm->asm-$(ARCH) symlink

symlinks:
	rm -f include/asm
	( cd include ; ln -sf asm-$(ARCH) asm)
	@if [ ! -d include/linux/modules ]; then \
		mkdir include/linux/modules; \
	fi

# split autoconf.h into include/linux/config/*

include/config/MARKER: scripts/split-include include/linux/autoconf.h
	scripts/split-include include/linux/autoconf.h include/config
	@ touch include/config/MARKER
	. scripts/mkversion > .tmpversion
	@mv -f .tmpversion .version

# Generate some files

$(TOPDIR)/include/linux/version.h: include/linux/version.h

include/linux/version.h: ./Makefile
	@echo Generating $@
	@. scripts/mkversion_h $@ $(KERNELRELEASE) $(VERSION) $(PATCHLEVEL) $(SUBLEVEL)

comma	:= ,

# ---------------------------------------------------------------------------
# Generate dependencies

depend dep: dep-files

dep-files: scripts/mkdep archdep include/linux/version.h
	scripts/mkdep -- `find $(FINDHPATH) -name SCCS -prune -o -follow -name \*.h ! -name modversions.h -print` > .hdepend
	$(MAKE) $(patsubst %,_sfdep_%,$(SUBDIRS)) _FASTDEP_ALL_SUB_DIRS="$(SUBDIRS)"
ifdef CONFIG_MODVERSIONS
	$(MAKE) update-modverfile
endif

# ---------------------------------------------------------------------------
# Modules

ifdef CONFIG_MODULES

#	Build modules

ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif

.PHONY: modules
modules: $(patsubst %, _mod_%, $(SUBDIRS))

.PHONY: $(patsubst %, _mod_%, $(SUBDIRS))
$(patsubst %, _mod_%, $(SUBDIRS)) : include/linux/version.h include/config/MARKER
	@$(MAKE) -C $(patsubst _mod_%, %, $@) CFLAGS="$(CFLAGS) $(MODFLAGS)" MAKING_MODULES=1 modules

#	Install modules

.PHONY: modules_install
modules_install: _modinst_ $(patsubst %, _modinst_%, $(SUBDIRS)) _modinst_post

.PHONY: _modinst_
_modinst_:
	@rm -rf $(MODLIB)/kernel
	@rm -f $(MODLIB)/build
	@mkdir -p $(MODLIB)/kernel
	@ln -s $(TOPDIR) $(MODLIB)/build

# If System.map exists, run depmod.  This deliberately does not have a
# dependency on System.map since that would run the dependency tree on
# vmlinux.  This depmod is only for convenience to give the initial
# boot a modules.dep even before / is mounted read-write.  However the
# boot script depmod is the master version.
ifeq "$(strip $(INSTALL_MOD_PATH))" ""
depmod_opts	:=
else
depmod_opts	:= -b $(INSTALL_MOD_PATH) -r
endif
.PHONY: _modinst_post
_modinst_post:
	if [ -r System.map ]; then $(DEPMOD) -ae -F System.map $(depmod_opts) $(KERNELRELEASE); fi

.PHONY: $(patsubst %, _modinst_%, $(SUBDIRS))
$(patsubst %, _modinst_%, $(SUBDIRS)) :
	@$(MAKE) -C $(patsubst _modinst_%, %, $@) modules_install

else # CONFIG_MODULES

# ---------------------------------------------------------------------------
# Modules not configured

modules modules_install: dummy
	@echo
	@echo "The present kernel configuration has modules disabled."
	@echo "Type 'make config' and enable loadable module support."
	@echo "Then build a kernel with module support enabled."
	@echo
	@exit 1

endif # CONFIG_MODULES

# ---------------------------------------------------------------------------

include Rules.make

# Build helpers in scripts/
# FIXME: do that in scripts/Makefile?

scripts/mkdep: scripts/mkdep.c
	$(HOSTCC) $(HOSTCFLAGS) -o scripts/mkdep scripts/mkdep.c

scripts/split-include: scripts/split-include.c
	$(HOSTCC) $(HOSTCFLAGS) -o scripts/split-include scripts/split-include.c

# Cleaning up
# ---------------------------------------------------------------------------

#	files removed with 'make clean'
CLEAN_FILES += \
	kernel/ksyms.lst include/linux/compile.h \
	vmlinux System.map \
	.tmp* \
	drivers/char/consolemap_deftbl.c drivers/video/promcon_tbl.c \
	drivers/char/conmakehash \
	drivers/char/drm/*-mod.c \
	drivers/pci/devlist.h drivers/pci/classlist.h drivers/pci/gen-devlist \
	drivers/zorro/devlist.h drivers/zorro/gen-devlist \
	sound/oss/bin2hex sound/oss/hex2hex \
	drivers/atm/fore200e_mkfirm drivers/atm/{pca,sba}*{.bin,.bin1,.bin2} \
	drivers/scsi/aic7xxx/aicasm/aicasm_gram.c \
	drivers/scsi/aic7xxx/aicasm/aicasm_scan.c \
	drivers/scsi/aic7xxx/aicasm/y.tab.h \
	drivers/scsi/aic7xxx/aicasm/aicasm \
	drivers/scsi/53c700_d.h \
	net/khttpd/make_times_h \
	net/khttpd/times.h \
	submenu*

# 	directories removed with 'make clean'
CLEAN_DIRS += \
	modules

# 	files removed with 'make mrproper'
MRPROPER_FILES += \
	include/linux/autoconf.h include/linux/version.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{afsk1200,afsk2666,fsk9600}.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{hapn4800,psk4800}.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{afsk2400_7,afsk2400_8}.h \
	drivers/net/hamradio/soundmodem/gentbl \
	sound/oss/*_boot.h sound/oss/.*.boot \
	sound/oss/msndinit.c \
	sound/oss/msndperm.c \
	sound/oss/pndsperm.c \
	sound/oss/pndspini.c \
	drivers/atm/fore200e_*_fw.c drivers/atm/.fore200e_*.fw \
	.version .config* config.in config.old \
	scripts/tkparse scripts/kconfig.tk scripts/kconfig.tmp \
	scripts/lxdialog/*.o scripts/lxdialog/lxdialog \
	.menuconfig.log \
	include/asm \
	.hdepend scripts/mkdep scripts/split-include scripts/docproc \
	$(TOPDIR)/include/linux/modversions.h \
	kernel.spec

# 	directories removed with 'make mrproper'
MRPROPER_DIRS += \
	include/config \
	$(TOPDIR)/include/linux/modules


clean:	archclean
	find . \( -name '*.[oas]' -o -name core -o -name '.*.flags' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	rm -f $(CLEAN_FILES)
	rm -rf $(CLEAN_DIRS)
	@$(MAKE) -C Documentation/DocBook clean

mrproper: clean archmrproper
	find . \( -size 0 -o -name .depend \) -type f -print | xargs rm -f
	rm -f $(MRPROPER_FILES)
	rm -rf $(MRPROPER_DIRS)
	@$(MAKE) -C Documentation/DocBook mrproper

distclean: mrproper
	rm -f core `find . \( -not -type d \) -and \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name '.SUMS' -o -size 0 \) -type f -print` TAGS tags

# Assorted miscellaneous targets
# ---------------------------------------------------------------------------

# Documentation targets

sgmldocs psdocs pdfdocs htmldocs:
	@$(MAKE) -C Documentation/DocBook $@


# RPM target
#
#	If you do a make spec before packing the tarball you can rpm -ta it

spec:
	. scripts/mkspec >kernel.spec

#	Build a tar ball, generate an rpm from it and pack the result
#	There arw two bits of magic here
#	1) The use of /. to avoid tar packing just the symlink
#	2) Removing the .dep files as they have source paths in them that
#	   will become invalid

rpm:	clean spec
	find . \( -size 0 -o -name .depend -o -name .hdepend \) -type f -print | xargs rm -f
	set -e; \
	cd $(TOPDIR)/.. ; \
	ln -sf $(TOPDIR) $(KERNELPATH) ; \
	tar -cvz --exclude CVS -f $(KERNELPATH).tar.gz $(KERNELPATH)/. ; \
	rm $(KERNELPATH) ; \
	cd $(TOPDIR) ; \
	. scripts/mkversion > .version ; \
	rpm -ta $(TOPDIR)/../$(KERNELPATH).tar.gz ; \
	rm $(TOPDIR)/../$(KERNELPATH).tar.gz

# Scripts to check various things for consistency

checkconfig:
	find * -name '*.[hcS]' -type f -print | sort | xargs $(PERL) -w scripts/checkconfig.pl

checkhelp:
	find * -name [cC]onfig.in -print | sort | xargs $(PERL) -w scripts/checkhelp.pl

checkincludes:
	find * -name '*.[hcS]' -type f -print | sort | xargs $(PERL) -w scripts/checkincludes.pl

# Generate tags for editors

TAGS: dummy
	{ find include/asm-${ARCH} -name '*.h' -print ; \
	find include -type d \( -name "asm-*" -o -name config \) -prune -o -name '*.h' -print ; \
	find $(SUBDIRS) init -name '*.[ch]' ; } | grep -v SCCS | etags -

# 	Exuberant ctags works better with -I
tags: dummy
	CTAGSF=`ctags --version | grep -i exuberant >/dev/null && echo "-I __initdata,__exitdata,EXPORT_SYMBOL,EXPORT_SYMBOL_NOVERS"`; \
	ctags $$CTAGSF `find include/asm-$(ARCH) -name '*.h'` && \
	find include -type d \( -name "asm-*" -o -name config \) -prune -o -name '*.h' -print | xargs ctags $$CTAGSF -a && \
	find $(SUBDIRS) init -name '*.[ch]' | xargs ctags $$CTAGSF -a

# Targets which will only descend into one subdir, not trying
# to link vmlinux afterwards
# FIXME: anybody still using this?

fs lib mm ipc kernel drivers net sound: dummy
	$(MAKE) CFLAGS="$(CFLAGS) $(CFLAGS_KERNEL)" AFLAGS="$(AFLAGS) $(AFLAGS_KERNEL)" $(subst $@, _dir_$@, $@)

# Make a backup
# FIXME anybody still using this?

backup: mrproper
	cd .. && tar cf - linux/ | gzip -9 > backup.gz
	sync

# Make checksums
# FIXME anybody still using this?

sums:
	find . -type f -print | sort | xargs sum > .SUMS
