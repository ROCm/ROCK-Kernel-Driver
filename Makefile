VERSION = 2
PATCHLEVEL = 5
SUBLEVEL = 45
EXTRAVERSION =

# *DOCUMENTATION*
# To see a list of typical targets execute "make help"
# More info can be located in ./Documentation/kbuild
# Comments in this file are targeted only to the developer, do not
# expect to learn how to build the kernel reading this file.

# We are using a recursive build, so we need to do a little thinking
# to get the ordering right.
#
# Most importantly: sub-Makefiles should only ever modify files in
# their own directory. If in some directory we have a dependency on
# a file in another dir (which doesn't happen often, but it's of
# unavoidable when linking the built-in.o targets which finally
# turn into vmlinux), we will call a sub make in that other dir, and
# after that we are sure that everything which is in that other dir
# is now up to date.
#
# The only cases where we need to modify files which have global
# effects are thus separated out and done before the recursive
# descending is started. They are now explicitly listed as the
# prepare rule.

KERNELRELEASE=$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

# SUBARCH tells the usermode build what the underlying arch is.  That is set
# first, and if a usermode build is happening, the "ARCH=um" on the command
# line overrides the setting of ARCH below.  If a native build is happening,
# then ARCH is assigned, getting whatever value it gets normally, and 
# SUBARCH is subsequently ignored.

SUBARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)
ARCH := $(SUBARCH)

KERNELPATH=kernel-$(shell echo $(KERNELRELEASE) | sed -e "s/-//g")

UTS_MACHINE := $(ARCH)

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(CURDIR)

HOSTCC  	= gcc
HOSTCXX  	= g++
HOSTCFLAGS	= -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer
HOSTCXXFLAGS	= -O2

CROSS_COMPILE 	=

# 	That's our default target when none is given on the command line

all:	vmlinux

# 	Decide whether to build built-in, modular, or both.
#	Normally, just do built-in.

KBUILD_MODULES :=
KBUILD_BUILTIN := 1

#	If we have only "make modules", don't compile built-in objects.

ifeq ($(MAKECMDGOALS),modules)
  KBUILD_BUILTIN :=
endif

#	If we have "make <whatever> modules", compile modules
#	in addition to whatever we do anyway.

ifneq ($(filter modules,$(MAKECMDGOALS)),)
  KBUILD_MODULES := 1
endif

#	Just "make" or "make all" shall build modules as well

ifeq ($(MAKECMDGOALS),)
  KBUILD_MODULES := 1
endif

ifneq ($(filter all,$(MAKECMDGOALS)),)
  KBUILD_MODULES := 1
endif

export KBUILD_MODULES KBUILD_BUILTIN KBUILD_VERBOSE

# Beautify output
# ---------------------------------------------------------------------------
#
# Normally, we echo the whole command before executing it. By making
# that echo $($(quiet)$(cmd)), we now have the possibility to set
# $(quiet) to choose other forms of output instead, e.g.
#
#         quiet_cmd_cc_o_c = Compiling $(RELDIR)/$@
#         cmd_cc_o_c       = $(CC) $(c_flags) -c -o $@ $<
#
# If $(quiet) is empty, the whole command will be printed.
# If it is set to "quiet_", only the short version will be printed. 
# If it is set to "silent_", nothing wil be printed at all, since
# the variable $(silent_cmd_cc_o_c) doesn't exist.

#	For now, leave verbose as default

ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 1
endif

MAKEFLAGS += --no-print-directory

# For maximum performance (+ possibly random breakage, uncomment
# the following)

#MAKEFLAGS += -rR

#	If the user wants quiet mode, echo short versions of the commands 
#	only

ifeq ($(KBUILD_VERBOSE),1)
  quiet =
  Q =
else
  quiet=quiet_
  Q = @
endif

#	If the user is running make -s (silent mode), suppress echoing of
#	commands

ifneq ($(findstring s,$(MAKEFLAGS)),)
  quiet=silent_
endif

export quiet Q KBUILD_VERBOSE

#	Paths to obj / src tree

src	:= .
obj	:= .
srctree := .
objtree := .

export srctree objtree

# 	Make variables (CC, etc...)

AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
GENKSYMS	= /sbin/genksyms
DEPMOD		= /sbin/depmod
KALLSYMS	= /sbin/kallsyms
PERL		= perl
MODFLAGS	= -DMODULE
CFLAGS_MODULE   = $(MODFLAGS)
AFLAGS_MODULE   = $(MODFLAGS)
CFLAGS_KERNEL	=
AFLAGS_KERNEL	=
NOSTDINC_FLAGS  = -nostdinc -iwithprefix include

CPPFLAGS	:= -D__KERNEL__ -Iinclude
CFLAGS 		:= $(CPPFLAGS) -Wall -Wstrict-prototypes -Wno-trigraphs -O2 \
	  	   -fomit-frame-pointer -fno-strict-aliasing -fno-common
AFLAGS		:= -D__ASSEMBLY__ $(CPPFLAGS)

export	VERSION PATCHLEVEL SUBLEVEL EXTRAVERSION KERNELRELEASE ARCH \
	CONFIG_SHELL TOPDIR HOSTCC HOSTCFLAGS CROSS_COMPILE AS LD CC \
	CPP AR NM STRIP OBJCOPY OBJDUMP MAKE GENKSYMS PERL UTS_MACHINE \
	HOSTCXX HOSTCXXFLAGS

export CPPFLAGS NOSTDINC_FLAGS OBJCOPYFLAGS LDFLAGS
export CFLAGS CFLAGS_KERNEL CFLAGS_MODULE 
export AFLAGS AFLAGS_KERNEL AFLAGS_MODULE

# The temporary file to save gcc -MD generated dependencies must not
# contain a comma
depfile = $(subst $(comma),_,$(@D)/.$(@F).d)

noconfig_targets := xconfig menuconfig config oldconfig randconfig \
		    defconfig allyesconfig allnoconfig allmodconfig \
		    clean mrproper distclean \
		    help tags TAGS sgmldocs psdocs pdfdocs htmldocs \
		    checkconfig checkhelp checkincludes

RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS \) -prune -o
RCS_TAR_IGNORE := --exclude SCCS --exclude BitKeeper --exclude .svn --exclude CVS

# Helpers built in scripts/
# ---------------------------------------------------------------------------

scripts/docproc scripts/fixdep scripts/split-include : scripts ;

.PHONY: scripts
scripts:
	+@$(Q)$(MAKE) -f scripts/Makefile.build obj=scripts

# Objects we will link into vmlinux / subdirs we need to visit
# ---------------------------------------------------------------------------

init-y		:= init/
drivers-y	:= drivers/ sound/
net-y		:= net/
libs-y		:= lib/
core-y		:= usr/
SUBDIRS		:=

ifeq ($(filter $(noconfig_targets),$(MAKECMDGOALS)),)

export include-config := 1

-include .config

endif

include arch/$(ARCH)/Makefile

core-y		+= kernel/ mm/ fs/ ipc/ security/ crypto/

SUBDIRS		+= $(patsubst %/,%,$(filter %/, $(init-y) $(init-m) \
		     $(core-y) $(core-m) $(drivers-y) $(drivers-m) \
		     $(net-y) $(net-m) $(libs-y) $(libs-m)))

ALL_SUBDIRS     := $(SUBDIRS) $(patsubst %/,%,$(filter %/, $(init-n) $(init-) \
		     $(core-n) $(core-) $(drivers-n) $(drivers-) \
		     $(net-n) $(net-) $(libs-n) $(libs-)))

init-y		:= $(patsubst %/, %/built-in.o, $(init-y))
core-y		:= $(patsubst %/, %/built-in.o, $(core-y))
drivers-y	:= $(patsubst %/, %/built-in.o, $(drivers-y))
net-y		:= $(patsubst %/, %/built-in.o, $(net-y))
libs-y		:= $(patsubst %/, %/lib.a, $(libs-y))

ifdef include-config

# Here goes the main Makefile
# ===========================================================================
#
# If the user gave a *config target, it'll be handled in another
# section below, since in this case we cannot include .config
# Same goes for other targets like clean/mrproper etc, which
# don't need .config, either

#	In this section, we need .config

-include ..config.cmd

ifdef CONFIG_MODULES
export EXPORT_FLAGS := -DEXPORT_SYMTAB
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

# Build vmlinux
# ---------------------------------------------------------------------------

#	This is a bit tricky: If we need to relink vmlinux, we want
#	the version number incremented, which means recompile init/version.o
#	and relink init/init.o. However, we cannot do this during the
#       normal descending-into-subdirs phase, since at that time
#       we cannot yet know if we will need to relink vmlinux.
#	So we descend into init/ inside the rule for vmlinux again.

vmlinux-objs := $(HEAD) $(init-y) $(core-y) $(libs-y) $(drivers-y) $(net-y)

quiet_cmd_vmlinux__ = LD      $@
define cmd_vmlinux__
	$(LD) $(LDFLAGS) $(LDFLAGS_vmlinux) $(HEAD) $(init-y) \
	--start-group \
	$(core-y) \
	$(libs-y) \
	$(drivers-y) \
	$(net-y) \
	--end-group \
	$(filter .tmp_kallsyms%,$^) \
	-o $@
endef

#	set -e makes the rule exit immediately on error

define rule_vmlinux__
	set -e
	$(if $(filter .tmp_kallsyms%,$^),,
	  echo '  Generating build number'
	  . scripts/mkversion > .tmp_version
	  mv -f .tmp_version .version
	  $(Q)$(MAKE) -f scripts/Makefile.build obj=init
	)
	$(call cmd,vmlinux__)
	echo 'cmd_$@ := $(cmd_vmlinux__)' > $(@D)/.$(@F).cmd
endef

define rule_vmlinux
	$(rule_vmlinux__)
	$(NM) $@ | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aUw] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > System.map
endef

LDFLAGS_vmlinux += -T arch/$(ARCH)/vmlinux.lds.s

#	Generate section listing all symbols and add it into vmlinux
#	It's a three stage process:
#	o .tmp_vmlinux1 has all symbols and sections, but __kallsyms is
#	  empty
#	  Running kallsyms on that gives as .tmp_kallsyms1.o with
#	  the right size
#	o .tmp_vmlinux2 now has a __kallsyms section of the right size,
#	  but due to the added section, some addresses have shifted
#	  From here, we generate a correct .tmp_kallsyms2.o
#	o The correct .tmp_kallsyms2.o is linked into the final vmlinux.

ifdef CONFIG_KALLSYMS

kallsyms.o := .tmp_kallsyms2.o

quiet_cmd_kallsyms = KSYM    $@
cmd_kallsyms = $(KALLSYMS) $< > $@

.tmp_kallsyms1.o: .tmp_vmlinux1
	$(call cmd,kallsyms)

.tmp_kallsyms2.o: .tmp_vmlinux2
	$(call cmd,kallsyms)

.tmp_vmlinux1: $(vmlinux-objs) arch/$(ARCH)/vmlinux.lds.s FORCE
	$(call if_changed_rule,vmlinux__)

.tmp_vmlinux2: $(vmlinux-objs) .tmp_kallsyms1.o arch/$(ARCH)/vmlinux.lds.s FORCE
	$(call if_changed_rule,vmlinux__)

endif

#	Finally the vmlinux rule

vmlinux: $(vmlinux-objs) $(kallsyms.o) arch/$(ARCH)/vmlinux.lds.s FORCE
	$(call if_changed_rule,vmlinux)

#	The actual objects are generated when descending, 
#	make sure no implicit rule kicks in

$(sort $(vmlinux-objs)): $(SUBDIRS) ;

# 	Handle descending into subdirectories listed in $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS): .hdepend prepare
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$@

#	Things we need done before we descend to build or make
#	module versions are listed in "prepare"

.PHONY: prepare
prepare: include/linux/version.h include/asm include/config/MARKER
	@echo '  Starting the build. KBUILD_BUILTIN=$(KBUILD_BUILTIN) KBUILD_MODULES=$(KBUILD_MODULES)'

#	This can be used by arch/$ARCH/Makefile to preprocess
#	their vmlinux.lds.S file

AFLAGS_vmlinux.lds.o += -P -C -U$(ARCH)

arch/$(ARCH)/vmlinux.lds.s: %.s: %.S scripts FORCE
	$(call if_changed_dep,as_s_S)

targets += arch/$(ARCH)/vmlinux.lds.s

# Single targets
# ---------------------------------------------------------------------------

%.s: %.c scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@
%.i: %.c scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@
%.o: %.c scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@
%.lst: %.c scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@
%.s: %.S scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@
%.o: %.S scripts FORCE
	$(Q)$(MAKE) -f scripts/Makefile.build obj=$(@D) $@

# 	FIXME: The asm symlink changes when $(ARCH) changes. That's
#	hard to detect, but I suppose "make mrproper" is a good idea
#	before switching between archs anyway.

include/asm:
	@echo '  Making asm->asm-$(ARCH) symlink'
	@ln -s asm-$(ARCH) $@

# 	Split autoconf.h into include/linux/config/*

include/config/MARKER: scripts/split-include include/linux/autoconf.h
	@echo '  SPLIT  include/linux/autoconf.h -> include/config/*'
	@scripts/split-include include/linux/autoconf.h include/config
	@touch $@

# 	if .config is newer than include/linux/autoconf.h, someone tinkered
# 	with it and forgot to run make oldconfig

include/linux/autoconf.h: .config
	+@$(call descend,scripts/kconfig,scripts/kconfig/conf)
	./scripts/kconfig/conf -s arch/$(ARCH)/Kconfig

# Generate some files
# ---------------------------------------------------------------------------

#	version.h changes when $(KERNELRELEASE) etc change, as defined in
#	this Makefile

uts_len := 64

include/linux/version.h: Makefile
	@if expr length "$(KERNELRELEASE)" \> $(uts_len) >/dev/null ; then \
	  echo '"$(KERNELRELEASE)" exceeds $(uts_len) characters' >&2; \
	  exit 1; \
	fi;
	@echo -n '  Generating $@'
	@(echo \#define UTS_RELEASE \"$(KERNELRELEASE)\"; \
	  echo \#define LINUX_VERSION_CODE `expr $(VERSION) \\* 65536 + $(PATCHLEVEL) \\* 256 + $(SUBLEVEL)`; \
	 echo '#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))'; \
	) > $@.tmp
	@$(update-if-changed)

# Generate module versions
# ---------------------------------------------------------------------------

# 	The targets are still named depend / dep for traditional
#	reasons, but the only thing we do here is generating
#	the module version checksums.

.PHONY: depend dep $(patsubst %,_sfdep_%,$(SUBDIRS))

depend dep: .hdepend

#	.hdepend is our (misnomed) marker for whether we've
#	generated module versions

make-versions := $(strip $(if $(filter dep depend,$(MAKECMDGOALS)),1) \
			 $(if $(wildcard .hdepend),,1))

.hdepend: prepare FORCE
ifneq ($(make-versions),)
	@$(MAKE) include/linux/modversions.h
	@touch $@
endif

ifdef CONFIG_MODVERSIONS

# 	Update modversions.h, but only if it would change.

.PHONY: __rm_tmp_export-objs
__rm_tmp_export-objs: 
	@rm -rf .tmp_export-objs

include/linux/modversions.h: $(patsubst %,_modver_%,$(SUBDIRS))
	@echo -n '  Generating $@'
	@( echo "#ifndef _LINUX_MODVERSIONS_H";\
	   echo "#define _LINUX_MODVERSIONS_H"; \
	   echo "#include <linux/modsetver.h>"; \
	   cd .tmp_export-objs >/dev/null; \
	   for f in `find modules -name \*.ver -print | sort`; do \
	     echo "#include <linux/$${f}>"; \
	   done; \
	   echo "#endif"; \
	) > $@.tmp; \
	$(update-if-changed)

.PHONY: $(patsubst %, _modver_%, $(SUBDIRS))
$(patsubst %, _modver_%, $(SUBDIRS)): __rm_tmp_export-objs
	$(Q)$(MAKE) -f scripts/Makefile.modver obj=$(patsubst _modver_%,%,$@)

else # !CONFIG_MODVERSIONS

.PHONY: include/linux/modversions.h

include/linux/modversions.h:

endif # CONFIG_MODVERSIONS

# ---------------------------------------------------------------------------
# Modules

ifdef CONFIG_MODULES

#	Build modules

ifdef CONFIG_MODVERSIONS
MODFLAGS += -include include/linux/modversions.h
endif

.PHONY: modules
modules: $(SUBDIRS)

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
	$(Q)$(MAKE) -f scripts/Makefile.modinst obj=$(patsubst _modinst_%,%,$@)
else # CONFIG_MODULES

# Modules not configured
# ---------------------------------------------------------------------------

modules modules_install: FORCE
	@echo
	@echo "The present kernel configuration has modules disabled."
	@echo "Type 'make config' and enable loadable module support."
	@echo "Then build a kernel with module support enabled."
	@echo
	@exit 1

endif # CONFIG_MODULES

# Generate asm-offsets.h 
# ---------------------------------------------------------------------------

define generate-asm-offsets.h
	(set -e; \
	 echo "#ifndef __ASM_OFFSETS_H__"; \
	 echo "#define __ASM_OFFSETS_H__"; \
	 echo "/*"; \
	 echo " * DO NOT MODIFY."; \
	 echo " *"; \
	 echo " * This file was generated by arch/$(ARCH)/Makefile"; \
	 echo " *"; \
	 echo " */"; \
	 echo ""; \
	 sed -ne "/^->/{s:^->\([^ ]*\) [\$$#]*\([^ ]*\) \(.*\):#define \1 \2 /* \3 */:; s:->::; p;}"; \
	 echo ""; \
	 echo "#endif" )
endef

# RPM target
# ---------------------------------------------------------------------------

#	If you do a make spec before packing the tarball you can rpm -ta it

spec:
	. scripts/mkspec >kernel.spec

#	Build a tar ball, generate an rpm from it and pack the result
#	There arw two bits of magic here
#	1) The use of /. to avoid tar packing just the symlink
#	2) Removing the .dep files as they have source paths in them that
#	   will become invalid

rpm:	clean spec
	find . $(RCS_FIND_IGNORE) \
		\( -size 0 -o -name .depend -o -name .hdepend \) \
		-type f -print | xargs rm -f
	set -e; \
	cd $(TOPDIR)/.. ; \
	ln -sf $(TOPDIR) $(KERNELPATH) ; \
	tar -cvz $(RCS_TAR_IGNORE) -f $(KERNELPATH).tar.gz $(KERNELPATH)/. ; \
	rm $(KERNELPATH) ; \
	cd $(TOPDIR) ; \
	. scripts/mkversion > .version ; \
	rpm -ta $(TOPDIR)/../$(KERNELPATH).tar.gz ; \
	rm $(TOPDIR)/../$(KERNELPATH).tar.gz

else # ifdef include-config

ifeq ($(filter-out $(noconfig_targets),$(MAKECMDGOALS)),)

# Targets which don't need .config
# ===========================================================================
#
# These targets basically have their own Makefile - not quite, but at
# least its own exclusive section in the same Makefile. The reason for
# this is the following:
# To know the configuration, the main Makefile has to include
# .config. That's a obviously a problem when .config doesn't exist
# yet, but that could be kludged around with only including it if it
# exists.
# However, the larger problem is: If you run make *config, make will
# include the old .config, then execute your *config. It will then
# notice that a piece it included (.config) did change and restart from
# scratch. Which will cause execution of *config again. You get the
# picture.
# If we don't explicitly let the Makefile know that .config is changed
# by *config (the old way), it won't reread .config after *config,
# thus working with possibly stale values - we don't that either.
#
# So we divide things: This part here is for making *config targets,
# and other targets which should work when no .config exists yet.
# The main part above takes care of the rest after a .config exists.

# Kernel configuration
# ---------------------------------------------------------------------------

.PHONY: oldconfig xconfig menuconfig config \
	make_with_config

scripts/kconfig/conf scripts/kconfig/mconf scripts/kconfig/qconf: scripts/fixdep FORCE
	+@$(call descend,scripts/kconfig,$@)

xconfig: scripts/kconfig/qconf
	./scripts/kconfig/qconf arch/$(ARCH)/Kconfig

menuconfig: scripts/kconfig/mconf
	$(Q)$(MAKE) -f scripts/Makefile.build obj=scripts lxdialog
	./scripts/kconfig/mconf arch/$(ARCH)/Kconfig

config: scripts/kconfig/conf
	./scripts/kconfig/conf arch/$(ARCH)/Kconfig

oldconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -o arch/$(ARCH)/Kconfig

randconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -r arch/$(ARCH)/Kconfig

allyesconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -y arch/$(ARCH)/Kconfig

allnoconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -n arch/$(ARCH)/Kconfig

allmodconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -m arch/$(ARCH)/Kconfig

defconfig: scripts/kconfig/conf
	./scripts/kconfig/conf -d arch/$(ARCH)/Kconfig

###
# Cleaning is done on three levels.
# make clean     Delete all automatically generated files, including
#                tools and firmware.
# make mrproper  Delete the current configuration, and related files
#                Any core files spread around is deleted as well
# make distclean Remove editor backup files, patch leftover files and the like

# Files removed with 'make clean'
CLEAN_FILES += vmlinux System.map MC*

# Files removed with 'make mrproper'
MRPROPER_FILES += \
	include/linux/autoconf.h include/linux/version.h \
	.version .config .config.old config.in config.old \
	.menuconfig.log \
	include/asm \
	.hdepend include/linux/modversions.h \
	tags TAGS kernel.spec \
	.tmp*

# Directories removed with 'make mrproper'
MRPROPER_DIRS += \
	.tmp_export-objs \
	include/config \
	include/linux/modules

# clean - Delete all intermediate files
#
clean-dirs += $(ALL_SUBDIRS) Documentation/DocBook scripts

$(addprefix _clean_,$(clean-dirs)):
	$(Q)$(MAKE) -f scripts/Makefile.clean obj=$(patsubst _clean_%,%,$@)

quiet_cmd_rmclean = RM  $$(CLEAN_FILES)
cmd_rmclean	  = rm -f $(CLEAN_FILES)
clean: archclean $(addprefix _clean_,$(clean-dirs))
	$(call cmd,rmclean)
	@find . $(RCS_FIND_IGNORE) \
	 	\( -name '*.[oas]' -o -name '.*.cmd' -o -name '.*.d' \
		-o -name '.*.tmp' \) -type f -print | xargs rm -f

# mrproper - delete configuration + modules + core files
#
quiet_cmd_mrproper = RM  $$(MRPROPER_DIRS) + $$(MRPROPER_FILES)
cmd_mrproper = rm -rf $(MRPROPER_DIRS) && rm -f $(MRPROPER_FILES)
mrproper distclean: clean archmrproper
	@echo '  Making $@ in the srctree'
	@find . $(RCS_FIND_IGNORE) \
	 	\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
	 	-o -name '.*.rej' -o -size 0 \
		-o -name '*%' -o -name '.*.cmd' -o -name 'core' \) \
		-type f -print | xargs rm -f
	$(call cmd,mrproper)

# Generate tags for editors
# ---------------------------------------------------------------------------

define all-sources
	( find . $(RCS_FIND_IGNORE) \
	       \( -name include -o -name arch \) -prune -o \
	       -name '*.[chS]' -print; \
	  find arch/$(ARCH) $(RCS_FIND_IGNORE) \
	       -name '*.[chS]' -print; \
	  find include $(RCS_FIND_IGNORE) \
	       \( -name config -o -name 'asm-*' \) -prune \
	       -o -name '*.[chS]' -print; \
	  find include/asm-$(ARCH) $(RCS_FIND_IGNORE) \
	       -name '*.[chS]' -print; \
	  find include/asm-generic $(RCS_FIND_IGNORE) \
	       -name '*.[chS]' -print )
endef

quiet_cmd_TAGS = MAKE   $@
cmd_TAGS = $(all-sources) | etags -

# 	Exuberant ctags works better with -I

quiet_cmd_tags = MAKE   $@
define cmd_tags
	rm -f $@; \
	CTAGSF=`ctags --version | grep -i exuberant >/dev/null && echo "-I __initdata,__exitdata,EXPORT_SYMBOL,EXPORT_SYMBOL_NOVERS"`; \
	$(all-sources) | xargs ctags $$CTAGSF -a
endef

TAGS: FORCE
	$(call cmd,TAGS)

tags: FORCE
	$(call cmd,tags)

# Brief documentation of the typical targets used
# ---------------------------------------------------------------------------

help:
	@echo  'Cleaning targets:'
	@echo  '  clean		- remove most generated files but keep the config'
	@echo  '  mrproper	- remove all generated files including the config'
	@echo  '  distclean	- mrproper + remove files generated by editors and patch'
	@echo  ''
	@echo  'Configuration targets:'
	@echo  '  oldconfig	- Update current config utilising a line-oriented program'
	@echo  '  menuconfig	- Update current config utilising a menu based program'
	@echo  '  xconfig	- Update current config utilising a X-based program'
	@echo  '  defconfig	- New config with default answer to all options'
	@echo  '  allmodconfig	- New config selecting modules when possible'
	@echo  '  allyesconfig	- New config where all options are accepted with yes'
	@echo  '  allnoconfig	- New minimal config'
	@echo  ''
	@echo  'Other generic targets:'
	@echo  '  all		- Build all targets marked with [*]'
	@echo  '  dep           - Create module version information'
	@echo  '* vmlinux	- Build the bare kernel'
	@echo  '* modules	- Build all modules'
	@echo  '  dir/file.[ois]- Build specified target only'
	@echo  '  rpm		- Build a kernel as an RPM package'
	@echo  '  tags/TAGS	- Generate tags file for editors'
	@echo  ''
	@echo  'Documentation targets:'
	@$(MAKE) --no-print-directory -f Documentation/DocBook/Makefile dochelp
	@echo  ''
	@echo  'Architecture specific targets ($(ARCH)):'
	@$(MAKE) --no-print-directory -f arch/$(ARCH)/boot/Makefile archhelp
	@echo  ''
	@echo  'Execute "make" or "make all" to build all targets marked with [*] '
	@echo  'For further info browse Documentation/kbuild/*'
 

# Documentation targets
# ---------------------------------------------------------------------------
sgmldocs psdocs pdfdocs htmldocs: scripts
	$(Q)$(MAKE) -f Documentation/DocBook/Makefile $@

# Scripts to check various things for consistency
# ---------------------------------------------------------------------------

checkconfig:
	find * $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort \
		| xargs $(PERL) -w scripts/checkconfig.pl

checkhelp:
	find * $(RCS_FIND_IGNORE) \
		-name [cC]onfig.in -print | sort \
		| xargs $(PERL) -w scripts/checkhelp.pl

checkincludes:
	find * $(RCS_FIND_IGNORE) \
		-name '*.[hcS]' -type f -print | sort \
		| xargs $(PERL) -w scripts/checkincludes.pl

else # ifneq ($(filter-out $(noconfig_targets),$(MAKECMDGOALS)),)

# We're called with both targets which do and do not need
# .config included. Handle them one after the other.
# ===========================================================================

%:: FORCE
	$(MAKE) $@

endif # ifeq ($(filter-out $(noconfig_targets),$(MAKECMDGOALS)),)
endif # ifdef include-config

# FIXME Should go into a make.lib or something 
# ===========================================================================

a_flags = -Wp,-MD,$(depfile) $(AFLAGS) $(NOSTDINC_FLAGS) \
	  $(modkern_aflags) $(EXTRA_AFLAGS) $(AFLAGS_$(*F).o)

quiet_cmd_as_s_S = CPP     $@
cmd_as_s_S       = $(CPP) $(a_flags)   -o $@ $< 

# read all saved command lines

targets := $(wildcard $(sort $(targets)))
cmd_files := $(wildcard .*.cmd $(foreach f,$(targets),$(dir $(f)).$(notdir $(f)).cmd))

ifneq ($(cmd_files),)
  include $(cmd_files)
endif

# execute the command and also postprocess generated .d dependencies
# file

if_changed_dep = $(if $(strip $? $(filter-out FORCE $(wildcard $^),$^)\
		          $(filter-out $(cmd_$(1)),$(cmd_$@))\
			  $(filter-out $(cmd_$@),$(cmd_$(1)))),\
	@set -e; \
	$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';) \
	$(cmd_$(1)); \
	scripts/fixdep $(depfile) $@ '$(cmd_$(1))' > $(@D)/.$(@F).tmp; \
	rm -f $(depfile); \
	mv -f $(@D)/.$(@F).tmp $(@D)/.$(@F).cmd)

# Usage: $(call if_changed_rule,foo)
# will check if $(cmd_foo) changed, or any of the prequisites changed,
# and if so will execute $(rule_foo)

if_changed_rule = $(if $(strip $? \
		               $(filter-out $(cmd_$(1)),$(cmd_$(@F)))\
			       $(filter-out $(cmd_$(@F)),$(cmd_$(1)))),\
	               @$(rule_$(1)))

# If quiet is set, only print short version of command

cmd = @$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) $(cmd_$(1))

define update-if-changed
	if [ -r $@ ] && cmp -s $@ $@.tmp; then \
		echo ' (unchanged)'; \
		rm -f $@.tmp; \
	else \
		echo ' (updated)'; \
		mv -f $@.tmp $@; \
	fi
endef
#	$(call descend,<dir>,<target>)
#	Recursively call a sub-make in <dir> with target <target>

descend = $(Q)$(MAKE) -f scripts/Makefile.build obj=$(1) $(2)

FORCE:
