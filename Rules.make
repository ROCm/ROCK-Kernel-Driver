#
# This file contains rules which are shared between multiple Makefiles.
#

# Some standard vars

comma   := ,
empty   :=
space   := $(empty) $(empty)

# Some bug traps
# ---------------------------------------------------------------------------

ifdef O_TARGET
$(error kbuild: $(obj)/Makefile - Usage of O_TARGET := $(O_TARGET) is obsolete in 2.5. Please fix!)
endif

ifdef L_TARGET
ifneq ($(L_TARGET),lib.a)
$(warning kbuild: $(obj)/Makefile - L_TARGET := $(L_TARGET) target shall be renamed to lib.a. Please fix!)
endif
endif

ifdef list-multi
$(warning kbuild: $(obj)/Makefile - list-multi := $(list-multi) is obsolete in 2.5. Please fix!)
endif

# Some paths for the Makefiles to use
# ---------------------------------------------------------------------------

# FIXME. For now, we leave it possible to use make -C or make -f
# to do work in subdirs.

ifndef obj
obj = .
CFLAGS := $(patsubst -I%,-I$(TOPDIR)/%,$(patsubst -I$(TOPDIR)/%,-I%,$(CFLAGS)))
AFLAGS := $(patsubst -I%,-I$(TOPDIR)/%,$(patsubst -I$(TOPDIR)/%,-I%,$(AFLAGS)))
endif

# For use in the quiet output
echo_target = $@

# Usage:
#
# $(obj)/target.o                     : target.o in the build dir
# $(src)/target.c                     : target.c in the source dir
# $(objtree)/include/linux/version.h  : Some file relative to the build
#					dir root
# $(srctree)/include/linux/module.h   : Some file relative to the source
#				        dir root
#
# $(obj) and $(src) can only be used in the section after
# include $(TOPDIR)/Rules.make, i.e for generated files and the like.
# Intentionally.
#
# We don't support separate source / object yet, so these are just
# placeholders for now

src := $(obj)

# Figure out what we need to build from the various variables
# ===========================================================================

# When an object is listed to be built compiled-in and modular,
# only build the compiled-in version

obj-m := $(filter-out $(obj-y),$(obj-m))

# Handle objects in subdirs
# ---------------------------------------------------------------------------
# o if we encounter foo/ in $(obj-y), replace it by foo/built-in.o
#   and add the directory to the list of dirs to descend into: $(subdir-y)
# o if we encounter foo/ in $(obj-m), remove it from $(obj-m) 
#   and add the directory to the list of dirs to descend into: $(subdir-m)

__subdir-y	:= $(patsubst %/,%,$(filter %/, $(obj-y)))
subdir-y	+= $(__subdir-y)
__subdir-m	:= $(patsubst %/,%,$(filter %/, $(obj-m)))
subdir-m	+= $(__subdir-m)
__subdir-n	:= $(patsubst %/,%,$(filter %/, $(obj-n)))
subdir-n	+= $(__subdir-n)
__subdir-	:= $(patsubst %/,%,$(filter %/, $(obj-)))
subdir-		+= $(__subdir-)
obj-y		:= $(patsubst %/, %/built-in.o, $(obj-y))
obj-m		:= $(filter-out %/, $(obj-m))

# Subdirectories we need to descend into

subdir-ym	:= $(sort $(subdir-y) $(subdir-m))

# export.o is never a composite object, since $(export-objs) has a
# fixed meaning (== objects which EXPORT_SYMBOL())
__obj-y = $(filter-out export.o,$(obj-y))
__obj-m = $(filter-out export.o,$(obj-m))

# if $(foo-objs) exists, foo.o is a composite object 
multi-used-y := $(sort $(foreach m,$(__obj-y), $(if $($(m:.o=-objs)), $(m))))
multi-used-m := $(sort $(foreach m,$(__obj-m), $(if $($(m:.o=-objs)), $(m))))

# Build list of the parts of our composite objects, our composite
# objects depend on those (obviously)
multi-objs-y := $(foreach m, $(multi-used-y), $($(m:.o=-objs)))
multi-objs-m := $(foreach m, $(multi-used-m), $($(m:.o=-objs)))

# $(subdir-obj-y) is the list of objects in $(obj-y) which do not live
# in the local directory
subdir-obj-y := $(foreach o,$(obj-y),$(if $(filter-out $(o),$(notdir $(o))),$(o)))

# Replace multi-part objects by their individual parts, look at local dir only
real-objs-y := $(foreach m, $(filter-out $(subdir-obj-y), $(obj-y)), $(if $($(m:.o=-objs)),$($(m:.o=-objs)),$(m))) $(EXTRA_TARGETS)
real-objs-m := $(foreach m, $(obj-m), $(if $($(m:.o=-objs)),$($(m:.o=-objs)),$(m)))

# Only build module versions for files which are selected to be built
export-objs := $(filter $(export-objs),$(real-objs-y) $(real-objs-m))

# Add subdir path

EXTRA_TARGETS	:= $(addprefix $(obj)/,$(EXTRA_TARGETS))
obj-y		:= $(addprefix $(obj)/,$(obj-y))
obj-m		:= $(addprefix $(obj)/,$(obj-m))
export-objs	:= $(addprefix $(obj)/,$(export-objs))
subdir-obj-y	:= $(addprefix $(obj)/,$(subdir-obj-y))
real-objs-y	:= $(addprefix $(obj)/,$(real-objs-y))
real-objs-m	:= $(addprefix $(obj)/,$(real-objs-m))
multi-used-y	:= $(addprefix $(obj)/,$(multi-used-y))
multi-used-m	:= $(addprefix $(obj)/,$(multi-used-m))
multi-objs-y	:= $(addprefix $(obj)/,$(multi-objs-y))
multi-objs-m	:= $(addprefix $(obj)/,$(multi-objs-m))
subdir-ym	:= $(addprefix $(obj)/,$(subdir-ym))

# The temporary file to save gcc -MD generated dependencies must not
# contain a comma
depfile = $(subst $(comma),_,$(@D)/.$(@F).d)

# We're called for one of three purposes:
# o fastdep: build module version files (.ver) for $(export-objs) in
#   the current directory
# o modules_install: install the modules in the current directory
# o build: When no target is given, first_rule is the default and
#   will build the built-in and modular objects in this dir
#   (or a subset thereof, depending on $(KBUILD_MODULES),$(KBUILD_BUILTIN)
#   When targets are given directly (like foo.o), we just build these
#   targets (That happens when someone does make some/dir/foo.[ois])

ifeq ($(MAKECMDGOALS),fastdep)

# ===========================================================================
# Module versions
# ===========================================================================

ifeq ($(strip $(export-objs)),)

# If we don't export any symbols in this dir, just descend
# ---------------------------------------------------------------------------

fastdep: $(subdir-ym)
	@/bin/true

else

# This sets version suffixes on exported symbols
# ---------------------------------------------------------------------------

MODVERDIR := include/linux/modules/

#
# Added the SMP separator to stop module accidents between uniprocessor
# and SMP Intel boxes - AC - from bits by Michael Chastain
#

ifdef CONFIG_SMP
	genksyms_smp_prefix := -p smp_
else
	genksyms_smp_prefix := 
endif

#	Don't include modversions.h, we're just about to generate it here.

CFLAGS_MODULE := $(filter-out -include linux/modversions.h,$(CFLAGS_MODULE))

$(addprefix $(MODVERDIR)/,$(real-objs-y:.o=.ver)): modkern_cflags := $(CFLAGS_KERNEL)
$(addprefix $(MODVERDIR)/,$(real-objs-m:.o=.ver)): modkern_cflags := $(CFLAGS_MODULE)
$(addprefix $(MODVERDIR)/,$(export-objs:.o=.ver)): export_flags   := -D__GENKSYMS__

c_flags = -Wp,-MD,$(depfile) $(CFLAGS) $(NOSTDINC_FLAGS) \
	  $(modkern_cflags) $(EXTRA_CFLAGS) $(CFLAGS_$(*F).o) \
	  -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) \
	  $(export_flags) 

# Our objects only depend on modversions.h, not on the individual .ver
# files (fix-dep filters them), so touch modversions.h if any of the .ver
# files changes

quiet_cmd_cc_ver_c = MKVER   include/linux/modules/$*.ver
cmd_cc_ver_c = $(CPP) $(c_flags) $< | $(GENKSYMS) $(genksyms_smp_prefix) \
		 -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp

# Okay, let's explain what's happening in rule_make_cc_ver_c:
# o echo the command
# o execute the command
# o If the $(CPP) fails, we won't notice because it's output is piped
#   to $(GENKSYMS) which does not fail. We recognize this case by
#   looking if the generated $(depfile) exists, though.
# o If the .ver file changed, touch modversions.h, which is our maker
#   of any changed .ver files.
# o Move command line and deps into their normal .*.cmd place.  

define rule_cc_ver_c
	$(if $($(quiet)cmd_cc_ver_c),echo '  $($(quiet)cmd_cc_ver_c)';) \
	$(cmd_cc_ver_c); \
	if [ ! -r $(depfile) ]; then exit 1; fi; \
	scripts/fixdep $(depfile) $@ '$(cmd_cc_ver_c)' > $(@D)/.$(@F).tmp; \
	rm -f $(depfile); \
	if [ ! -r $@ ] || cmp -s $@ $@.tmp; then \
	  touch include/linux/modversions.h; \
	fi; \
	mv -f $@.tmp $@
	mv -f $(@D)/.$(@F).tmp $(@D)/.$(@F).cmd
endef

$(MODVERDIR)/%.ver: %.c FORCE
	@$(call if_changed_rule,cc_ver_c)

targets := $(addprefix $(MODVERDIR)/,$(export-objs:.o=.ver))

fastdep: $(targets) $(subdir-ym)
	@mkdir -p $(dir $(addprefix .tmp_export-objs/modules/,$(export-objs:.o=.ver)))
	@touch $(addprefix .tmp_export-objs/modules/,$(export-objs:.o=.ver))

endif # export-objs 

else # ! fastdep
ifeq ($(MAKECMDGOALS),modules_install)

# ==========================================================================
# Installing modules
# ==========================================================================

quiet_cmd_modules_install = INSTALL $(obj-m)
cmd_modules_install = mkdir -p $(MODLIB)/kernel/$(obj); \
		      cp $(obj-m) $(MODLIB)/kernel/$(obj)

.PHONY: modules_install

modules_install: $(subdir-ym)
ifneq ($(obj-m),)
	$(call cmd,modules_install)
else
	@/bin/true
endif

else # ! modules_install

# ==========================================================================
# Building
# ==========================================================================

# If a Makefile does define neither O_TARGET nor L_TARGET,
# use a standard O_TARGET named "built-in.o"

ifndef O_TARGET
ifndef L_TARGET
O_TARGET := $(obj)/built-in.o
endif
endif

ifdef L_TARGET
L_TARGET := $(obj)/$(L_TARGET)
endif

first_rule: $(if $(KBUILD_BUILTIN),$(O_TARGET) $(L_TARGET) $(EXTRA_TARGETS)) \
	    $(if $(KBUILD_MODULES),$(obj-m)) \
	    $(subdir-ym)
	@/bin/true

# Compile C sources (.c)
# ---------------------------------------------------------------------------

# Default is built-in, unless we know otherwise
modkern_cflags := $(CFLAGS_KERNEL)

$(real-objs-m)        : modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.i)  : modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.lst): modkern_cflags := $(CFLAGS_MODULE)

$(export-objs)        : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.i)  : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.s)  : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.lst): export_flags   := $(EXPORT_FLAGS)

c_flags = -Wp,-MD,$(depfile) $(CFLAGS) $(NOSTDINC_FLAGS) \
	  $(modkern_cflags) $(EXTRA_CFLAGS) $(CFLAGS_$(*F).o) \
	  -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) \
	  $(export_flags) 

quiet_cmd_cc_s_c = CC      $(echo_target)
cmd_cc_s_c       = $(CC) $(c_flags) -S -o $@ $< 

%.s: %.c FORCE
	$(call if_changed_dep,cc_s_c)

quiet_cmd_cc_i_c = CPP     $(echo_target)
cmd_cc_i_c       = $(CPP) $(c_flags)   -o $@ $<

%.i: %.c FORCE
	$(call if_changed_dep,cc_i_c)

quiet_cmd_cc_o_c = CC      $(echo_target)
cmd_cc_o_c       = $(CC) $(c_flags) -c -o $@ $<

%.o: %.c FORCE
	$(call if_changed_dep,cc_o_c)

quiet_cmd_cc_lst_c = MKLST   $(echo_target)
cmd_cc_lst_c       = $(CC) $(c_flags) -g -c -o $*.o $< && sh scripts/makelst $*.o System.map $(OBJDUMP) > $@

%.lst: %.c FORCE
	$(call if_changed_dep,cc_lst_c)

# Compile assembler sources (.S)
# ---------------------------------------------------------------------------

modkern_aflags := $(AFLAGS_KERNEL)

$(real-objs-m)      : modkern_aflags := $(AFLAGS_MODULE)
$(real-objs-m:.o=.s): modkern_aflags := $(AFLAGS_MODULE)

a_flags = -Wp,-MD,$(depfile) $(AFLAGS) $(NOSTDINC_FLAGS) \
	  $(modkern_aflags) $(EXTRA_AFLAGS) $(AFLAGS_$(*F).o)

quiet_cmd_as_s_S = CPP     $(echo_target)
cmd_as_s_S       = $(CPP) $(a_flags)   -o $@ $< 

%.s: %.S FORCE
	$(call if_changed_dep,as_s_S)

quiet_cmd_as_o_S = AS      $(echo_target)
cmd_as_o_S       = $(CC) $(a_flags) -c -o $@ $<

%.o: %.S FORCE
	$(call if_changed_dep,as_o_S)

targets += $(real-objs-y) $(real-objs-m) $(EXTRA_TARGETS) $(MAKECMDGOALS)

# Build the compiled-in targets
# ---------------------------------------------------------------------------

# To build objects in subdirs, we need to descend into the directories
$(sort $(subdir-obj-y)): $(subdir-ym) ;

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
quiet_cmd_link_o_target = LD      $(echo_target)
# If the list of objects to link is empty, just create an empty O_TARGET
cmd_link_o_target = $(if $(strip $(obj-y)),\
		      $(LD) $(LDFLAGS) $(EXTRA_LDFLAGS) -r -o $@ $(filter $(obj-y), $^),\
		      rm -f $@; $(AR) rcs $@)

$(O_TARGET): $(obj-y) FORCE
	$(call if_changed,link_o_target)

targets += $(O_TARGET)
endif # O_TARGET

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
quiet_cmd_link_l_target = AR      $(echo_target)
cmd_link_l_target = rm -f $@; $(AR) $(EXTRA_ARFLAGS) rcs $@ $(obj-y)

$(L_TARGET): $(obj-y) FORCE
	$(call if_changed,link_l_target)

targets += $(L_TARGET)
endif

#
# Rule to link composite objects
#

quiet_cmd_link_multi = LD      $(echo_target)
cmd_link_multi = $(LD) $(LDFLAGS) $(EXTRA_LDFLAGS) -r -o $@ $(filter $(addprefix $(obj)/,$($(subst $(obj)/,,$(@:.o=-objs)))),$^)

# We would rather have a list of rules like
# 	foo.o: $(foo-objs)
# but that's not so easy, so we rather make all composite objects depend
# on the set of all their parts
$(multi-used-y) : %.o: $(multi-objs-y) FORCE
	$(call if_changed,link_multi)

$(multi-used-m) : %.o: $(multi-objs-m) FORCE
	$(call if_changed,link_multi)

targets += $(multi-used-y) $(multi-used-m)

# Compile programs on the host
# ===========================================================================

host-progs-single     := $(foreach m,$(host-progs),$(if $($(m)-objs),,$(m)))
host-progs-multi      := $(foreach m,$(host-progs),$(if $($(m)-objs),$(m)))
host-progs-multi-objs := $(foreach m,$(host-progs-multi),$($(m)-objs))
host-progs	      := $(addprefix $(obj)/,$(host-progs))
host-progs-single     := $(addprefix $(obj)/,$(host-progs-single))
host-progs-multi      := $(addprefix $(obj)/,$(host-progs-multi))
host-progs-multi-objs := $(addprefix $(obj)/,$(host-progs-multi-objs))

quiet_cmd_host_cc__c  = HOSTCC  $(echo_target)
cmd_host_cc__c        = $(HOSTCC) -Wp,-MD,$(depfile) \
			$(HOSTCFLAGS) $(HOST_EXTRACFLAGS) \
			$(HOST_LOADLIBES) -o $@ $<

$(host-progs-single): %: %.c FORCE
	$(call if_changed_dep,host_cc__c)

quiet_cmd_host_cc_o_c = HOSTCC  $(echo_target)
cmd_host_cc_o_c       = $(HOSTCC) -Wp,-MD,$(depfile) \
			$(HOSTCFLAGS) $(HOST_EXTRACFLAGS) -c -o $@ $<

$(host-progs-multi-objs): %.o: %.c FORCE
	$(call if_changed_dep,host_cc_o_c)

quiet_cmd_host_cc__o  = HOSTLD  $(echo_target)
cmd_host_cc__o        = $(HOSTCC) $(HOSTLDFLAGS) -o $@ $(addprefix $(obj)/,$($(subst $(obj)/,,$@)-objs)) \
			$(HOST_LOADLIBES)

$(host-progs-multi): %: $(host-progs-multi-objs) FORCE
	$(call if_changed,host_cc__o)

targets += $(host-progs-single) $(host-progs-multi-objs) $(host-progs-multi) 

endif # ! modules_install
endif # ! fastdep

# Shipped files
# ===========================================================================

quiet_cmd_shipped = SHIPPED $(echo_target)
cmd_shipped = cat $< > $@

%:: %_shipped
	$(call cmd,shipped)

# Commands useful for building a boot image
# ===========================================================================
# 
#	Use as following:
#
#	target: source(s) FORCE
#		$(if_changed,ld/objcopy/gzip)
#
#	and add target to EXTRA_TARGETS so that we know we have to
#	read in the saved command line

# Linking
# ---------------------------------------------------------------------------

quiet_cmd_ld = LD      $(echo_target)
cmd_ld = $(LD) $(LDFLAGS) $(EXTRA_LDFLAGS) $(LDFLAGS_$(@F)) \
	       $(filter-out FORCE,$^) -o $@ 

# Objcopy
# ---------------------------------------------------------------------------

quiet_cmd_objcopy = OBJCOPY $(echo_target)
cmd_objcopy = $(OBJCOPY) $(OBJCOPYFLAGS) $< $@

# Gzip
# ---------------------------------------------------------------------------

quiet_cmd_gzip = GZIP    $(echo_target)
cmd_gzip = gzip -f -9 < $< > $@

# ===========================================================================
# Generic stuff
# ===========================================================================

# Descending
# ---------------------------------------------------------------------------

.PHONY: $(subdir-ym)

$(subdir-ym):
	+@$(call descend,$@,$(MAKECMDGOALS))

# Add FORCE to the prequisites of a target to force it to be always rebuilt.
# ---------------------------------------------------------------------------

.PHONY: FORCE

FORCE:

#
# This sets version suffixes on exported symbols
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#

# ---------------------------------------------------------------------------
# Check if command line has changed

# Usage:
# normally one uses rules like
#
# %.o: %.c
# 	<command line>
#
# However, these only rebuild the target when the source has changed,
# but not when e.g. the command or the flags on the command line changed.
#
# This extension allows to do the following:
#
# command = <command line>
#
# %.o: %.c dummy
#	$(call if_changed,command)
#
# which will make sure to rebuild the target when either its prerequisites
# change or the command line changes
#
# The magic works as follows:
# The addition of dummy to the dependencies causes the rule for rebuilding
# to be always executed. However, the if_changed function will generate
# an empty command when 
# o none of the prequesites changed (i.e $? is empty)
# o the command line did not change (we compare the old command line,
#   which is saved in .<target>.o, to the current command line using
#   the two filter-out commands)

# Read all saved command lines and dependencies for the $(targets) we
# may be building above, using $(if_changed{,_dep}). As an
# optimization, we don't need to read them if the target does not
# exist, we will rebuild anyway in that case.

targets := $(wildcard $(sort $(targets)))
cmd_files := $(wildcard $(foreach f,$(targets),$(dir $(f)).$(notdir $(f)).cmd))

ifneq ($(cmd_files),)
  include $(cmd_files)
endif

# function to only execute the passed command if necessary

if_changed = $(if $(strip $? \
		          $(filter-out $(cmd_$(1)),$(cmd_$@))\
			  $(filter-out $(cmd_$@),$(cmd_$(1)))),\
	@set -e; \
	$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';) \
	$(cmd_$(1)); \
	echo 'cmd_$@ := $(cmd_$(1))' > $(@D)/.$(@F).cmd)


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
		               $(filter-out $(cmd_$(1)),$(cmd_$@))\
			       $(filter-out $(cmd_$@),$(cmd_$(1)))),\
			@set -e; \
			mkdir -p $(dir $@); \
			$(rule_$(1)))

# If quiet is set, only print short version of command

cmd = @$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) $(cmd_$(1))

# do_cmd is a shorthand used to support both compressed, verbose
# and silent output in a single line.
# Compared to cmd described avobe, do_cmd does no rely on any variables 
# previously assigned a value.
#
# Usage $(call do_cmd,CMD   $@,cmd_to_execute bla bla)
# Example:
# $(call do_cmd,CP $@,cp -b $< $@)
# make -s => nothing will be printed
# make KBUILD_VERBOSE=1 => cp -b path/to/src.file path/to/dest.file
# make KBUILD_VERBOSE=0 =>   CP path/to/dest.file
define do_cmd
        @$(if $(filter quiet_,$(quiet)), echo '  $(1)' &&,
           $(if $(filter silent_,$(quiet)),,
             echo "$(2)" &&)) \
        $(2)
endef

#	$(call descend,<dir>,<target>)
#	Recursively call a sub-make in <dir> with target <target> 

ifeq ($(KBUILD_VERBOSE),1)
descend = echo '$(MAKE) -f $(1)/Makefile $(2)';
endif
descend += $(MAKE) -f $(1)/Makefile obj=$(1) $(2)
