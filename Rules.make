#
# This file contains rules which are shared between multiple Makefiles.
#

# Some standard vars

comma   := ,
empty   :=
space   := $(empty) $(empty)

# Figure out paths
# ---------------------------------------------------------------------------
# Find the path relative to the toplevel dir, $(RELDIR), and express
# the toplevel dir as a relative path from this dir, $(TOPDIR_REL)

ifeq ($(findstring $(TOPDIR),$(CURDIR)),)
  # Can only happen when something is built out of tree
  RELDIR := $(CURDIR)
  TOPDIR_REL := $(TOPDIR)
else
  RELDIR := $(subst $(TOPDIR)/,,$(CURDIR))
  TOPDIR_REL := $(subst $(space),,$(foreach d,$(subst /, ,$(RELDIR)),../))
endif

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

# If a dir is selected in $(subdir-y) and also mentioned in $(mod-subdirs),
# add it to $(subdir-m)

both-m          := $(filter $(mod-subdirs), $(subdir-y))
subdir-ym	:= $(sort $(subdir-y) $(subdir-m))
subdir-ymn	:= $(sort $(subdir-ym) $(subdir-n) $(subdir-))

# export.o is never a composite object, since $(export-objs) has a
# fixed meaning (== objects which EXPORT_SYMBOL())
__obj-y = $(filter-out export.o,$(obj-y))
__obj-m = $(filter-out export.o,$(obj-m))

# if $(foo-objs) exists, foo.o is a composite object 
__multi-used-y := $(sort $(foreach m,$(__obj-y), $(if $($(m:.o=-objs)), $(m))))
__multi-used-m := $(sort $(foreach m,$(__obj-m), $(if $($(m:.o=-objs)), $(m))))

# FIXME: Rip this out later
# Backwards compatibility: if a composite object is listed in
# $(list-multi), skip it here, since the Makefile will have an explicit
# link rule for it

multi-used-y := $(filter-out $(list-multi),$(__multi-used-y))
multi-used-m := $(filter-out $(list-multi),$(__multi-used-m))

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

# Get things started.
# ==========================================================================

ifndef O_TARGET
ifndef L_TARGET
O_TARGET := built-in.o
endif
endif

#	The echo suppresses the "Nothing to be done for first_rule"
first_rule: $(if $(KBUILD_BUILTIN),$(O_TARGET) $(L_TARGET) $(EXTRA_TARGETS)) \
	    $(if $(KBUILD_MODULES),$(obj-m)) \
	    sub_dirs
	@echo -n

# Compile C sources (.c)
# ---------------------------------------------------------------------------

# If we don't know if built-in or modular, assume built-in.
# Only happens in Makefiles which override the default first_rule:
modkern_cflags := $(CFLAGS_KERNEL)

$(real-objs-y)        : modkern_cflags := $(CFLAGS_KERNEL)
$(real-objs-y:.o=.i)  : modkern_cflags := $(CFLAGS_KERNEL)
$(real-objs-y:.o=.s)  : modkern_cflags := $(CFLAGS_KERNEL)
$(real-objs-y:.o=.lst): modkern_cflags := $(CFLAGS_KERNEL)

$(real-objs-m)        : modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.i)  : modkern_cflags := $(CFLAGS_MODULE)
$(real-objs-m:.o=.lst): modkern_cflags := $(CFLAGS_MODULE)

$(export-objs)        : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.i)  : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.s)  : export_flags   := $(EXPORT_FLAGS)
$(export-objs:.o=.lst): export_flags   := $(EXPORT_FLAGS)

c_flags = $(CFLAGS) $(NOSTDINC_FLAGS) $(modkern_cflags) $(EXTRA_CFLAGS) $(CFLAGS_$(*F).o) -DKBUILD_BASENAME=$(subst $(comma),_,$(subst -,_,$(*F))) $(export_flags) 

quiet_cmd_cc_s_c = CC     $(RELDIR)/$@
cmd_cc_s_c       = $(CC) $(c_flags) -S -o $@ $< 

%.s: %.c FORCE
	$(call cmd,cmd_cc_s_c)

quiet_cmd_cc_i_c = CPP    $(RELDIR)/$@
cmd_cc_i_c       = $(CPP) $(c_flags)   -o $@ $<

%.i: %.c FORCE
	$(call cmd,cmd_cc_i_c)

quiet_cmd_cc_o_c = CC     $(RELDIR)/$@
cmd_cc_o_c       = $(CC) -Wp,-MD,.$(subst /,_,$@).d $(c_flags) -c -o $@ $<

%.o: %.c FORCE
	$(call if_changed_dep,cc_o_c)

quiet_cmd_cc_lst_c = Generating $(RELDIR)/$@
cmd_cc_lst_c     = $(CC) $(c_flags) -g -c -o $*.o $< && $(TOPDIR)/scripts/makelst $*.o $(TOPDIR)/System.map $(OBJDUMP) > $@

%.lst: %.c FORCE
	$(call cmd,cmd_cc_lst_c)

# Compile assembler sources (.S)
# ---------------------------------------------------------------------------

# FIXME (s.a.)
modkern_aflags := $(AFLAGS_KERNEL)

$(real-objs-y)      : modkern_aflags := $(AFLAGS_KERNEL)
$(real-objs-y:.o=.s): modkern_aflags := $(AFLAGS_KERNEL)

$(real-objs-m)      : modkern_aflags := $(AFLAGS_MODULE)
$(real-objs-m:.o=.s): modkern_aflags := $(AFLAGS_MODULE)

a_flags = $(AFLAGS) $(NOSTDINC_FLAGS) $(modkern_aflags) $(EXTRA_AFLAGS) $(AFLAGS_$(*F).o)

quiet_cmd_as_s_S = CPP    $(RELDIR)/$@
cmd_as_s_S       = $(CPP) $(a_flags)   -o $@ $< 

%.s: %.S FORCE
	$(call cmd,cmd_as_s_S)

quiet_cmd_as_o_S = AS     $(RELDIR)/$@
cmd_as_o_S       = $(CC) -Wp,-MD,.$(subst /,_,$@).d $(a_flags) -c -o $@ $<

%.o: %.S FORCE
	$(call if_changed_dep,as_o_S)

# If a Makefile does define neither O_TARGET nor L_TARGET,
# use a standard O_TARGET named "built-in.o"

# Build the compiled-in targets
# ---------------------------------------------------------------------------

# To build objects in subdirs, we need to descend into the directories
$(sort $(subdir-obj-y)): sub_dirs ;

#
# Rule to compile a set of .o files into one .o file
#
ifdef O_TARGET
quiet_cmd_link_o_target = LD     $(RELDIR)/$@
# If the list of objects to link is empty, just create an empty O_TARGET
cmd_link_o_target = $(if $(strip $(obj-y)),\
		      $(LD) $(EXTRA_LDFLAGS) -r -o $@ $(filter $(obj-y), $^),\
		      rm -f $@; $(AR) rcs $@)

$(O_TARGET): $(obj-y) FORCE
	$(call if_changed,cmd_link_o_target)
endif # O_TARGET

#
# Rule to compile a set of .o files into one .a file
#
ifdef L_TARGET
quiet_cmd_link_l_target = AR     $(RELDIR)/$@
cmd_link_l_target = rm -f $@; $(AR) $(EXTRA_ARFLAGS) rcs $@ $(obj-y)

$(L_TARGET): $(obj-y) FORCE
	$(call if_changed,cmd_link_l_target)
endif

#
# Rule to link composite objects
#

quiet_cmd_link_multi = LD     $(RELDIR)/$@
cmd_link_multi = $(LD) $(EXTRA_LDFLAGS) -r -o $@ $(filter $($(basename $@)-objs),$^)

# We would rather have a list of rules like
# 	foo.o: $(foo-objs)
# but that's not so easy, so we rather make all composite objects depend
# on the set of all their parts
$(multi-used-y) : %.o: $(multi-objs-y) FORCE
	$(call if_changed,cmd_link_multi)

$(multi-used-m) : %.o: $(multi-objs-m) FORCE
	$(call if_changed,cmd_link_multi)

# Compile programs on the host
# FIXME: handle dependencies
# ===========================================================================

host-progs-single     := $(foreach m,$(host-progs),$(if $($(m)-objs),,$(m)))
host-progs-multi      := $(foreach m,$(host-progs),$(if $($(m)-objs),$(m)))
host-progs-multi-objs := $(foreach m,$(host-progs-multi),$($(m)-objs))

quiet_cmd_host_cc__c  = HOSTCC $(RELDIR)/$@
cmd_host_cc__c        = $(HOSTCC) -Wp,-MD,.$(subst /,_,$@).d \
			$(HOSTCFLAGS) $(HOST_EXTRACFLAGS) -o $@ $<

$(host-progs-single): %: %.c FORCE
	$(call if_changed_dep,host_cc__c)

quiet_cmd_host_cc_o_c = HOSTCC $(RELDIR)/$@
cmd_host_cc_o_c       = $(HOSTCC) -Wp,-MD,.$(subst /,_,$@).d \
			$(HOSTCFLAGS) $(HOST_EXTRACFLAGS) -c -o $@ $<

$(host-progs-multi-objs): %.o: %.c FORCE
	$(call if_changed_dep,host_cc_o_c)

quiet_cmd_host_cc__o  = HOSTLD $(RELDIR)/$@
cmd_host_cc__o        = $(HOSTCC) $(HOSTLDFLAGS) -o $@ $($@-objs) \
			$(HOST_LOADLIBES)

$(host-progs-multi): %: $(host-progs-multi-objs) FORCE
	$(call if_changed,cmd_host_cc__o)


# Descending when making module versions
# ---------------------------------------------------------------------------

fastdep-list := $(addprefix _sfdep_,$(subdir-ymn))

.PHONY: fastdep $(fastdep-list)

fastdep: $(fastdep-list)

$(fastdep-list):
	@$(MAKE) -C $(patsubst _sfdep_%,%,$@) fastdep

# Descending when building
# ---------------------------------------------------------------------------

subdir-list := $(addprefix _subdir_,$(subdir-ym))

.PHONY: sub_dirs $(subdir-list)

sub_dirs: $(subdir-list)

$(subdir-list):
	@$(MAKE) -C $(patsubst _subdir_%,%,$@)

# Descending and installing modules
# ---------------------------------------------------------------------------

modinst-list := $(addprefix _modinst_,$(subdir-ym))

.PHONY: modules_install _modinst_ $(modinst-list)

modules_install: $(modinst-list)
ifneq ($(obj-m),)
	@echo Installing modules in $(MODLIB)/kernel/$(RELDIR)
	@mkdir -p $(MODLIB)/kernel/$(RELDIR)
	@cp $(obj-m) $(MODLIB)/kernel/$(RELDIR)
else
	@echo -n
endif

$(modinst-list):
	@$(MAKE) -C $(patsubst _modinst_%,%,$@) modules_install

# Add FORCE to the prequisites of a target to force it to be always rebuilt.
# ---------------------------------------------------------------------------

.PHONY: FORCE

FORCE:

#
# This is useful for testing
# FIXME: really?
script:
	$(SCRIPT)

#
# This sets version suffixes on exported symbols
# Separate the object into "normal" objects and "exporting" objects
# Exporting objects are: all objects that define symbol tables
#

ifdef CONFIG_MODVERSIONS
ifneq "$(strip $(export-objs))" ""

MODVERDIR := $(TOPDIR)/include/linux/modules/$(RELDIR)

#
# Added the SMP separator to stop module accidents between uniprocessor
# and SMP Intel boxes - AC - from bits by Michael Chastain
#

ifdef CONFIG_SMP
	genksyms_smp_prefix := -p smp_
else
	genksyms_smp_prefix := 
endif

# We don't track dependencies for .ver files, so we FORCE to check
# them always (i.e. always at "make dep" time).

quiet_cmd_create_ver = Creating include/linux/modules/$(RELDIR)/$*.ver
cmd_create_ver = $(CC) $(CFLAGS) $(EXTRA_CFLAGS) -E -D__GENKSYMS__ $< | \
		 $(GENKSYMS) $(genksyms_smp_prefix) -k $(VERSION).$(PATCHLEVEL).$(SUBLEVEL) > $@.tmp

$(MODVERDIR)/%.ver: %.c FORCE
	@mkdir -p $(dir $@)
	@$(call cmd,cmd_create_ver)
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then \
	  rm -f $@.tmp; \
	else \
	  touch $(TOPDIR)/include/linux/modversions.h; \
	  mv -f $@.tmp $@; \
	fi

# updates .ver files but not modversions.h
fastdep: $(addprefix $(MODVERDIR)/,$(export-objs:.o=.ver))
ifneq ($(export-objs),)
	@mkdir -p $(TOPDIR)/.tmp_export-objs/modules/$(RELDIR)
	@touch $(addprefix $(TOPDIR)/.tmp_export-objs/modules/$(RELDIR)/,$(export-objs:.o=.ver))
endif


endif # export-objs 

endif # CONFIG_MODVERSIONS

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

# read all saved command lines and dependencies

cmd_files := $(wildcard .*.cmd)
ifneq ($(cmd_files),)
  include $(cmd_files)
endif

# function to only execute the passed command if necessary

if_changed = $(if $(strip $? \
		          $(filter-out $($(1)),$(cmd_$(@F)))\
			  $(filter-out $(cmd_$(@F)),$($(1)))),\
	       @$(if $($(quiet)$(1)),echo '  $($(quiet)$(1))' &&) $($(1)) && echo 'cmd_$@ := $($(1))' > $(@D)/.$(@F).cmd)


# execute the command and also postprocess generated .d dependencies
# file

if_changed_dep = $(if $(strip $? \
		          $(filter-out $(cmd_$(1)),$(cmd_$@))\
			  $(filter-out $(cmd_$@),$(cmd_$(1)))),\
	@set -e; \
	$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';) \
	$(cmd_$(1)); \
	$(TOPDIR)/scripts/fixdep $(subst /,_,$@) $(TOPDIR) '$(cmd_$(1))' > .$(subst /,_,$@).tmp; \
	rm -f .$(subst /,_,$@).d; \
	mv -f .$(subst /,_,$@).tmp .$(subst /,_,$@).cmd )

# If quiet is set, only print short version of command

cmd = @$(if $($(quiet)$(1)),echo '  $($(quiet)$(1))' &&) $($(1))
