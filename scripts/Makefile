###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# fix-dep: 	 Used to generate dependency information during build process
# split-include: Divide all config symbols up in a number of files in
#                include/config/...
# docproc: 	 Preprocess .tmpl file in order to generate .sgml documentation
# conmakehash:	 Create arrays for initializing the kernel console tables

EXTRA_TARGETS := fixdep split-include docproc conmakehash

subdir-	:= lxdialog kconfig

# Yikes. We need to build this stuff here even if the user only wants
# modules.

KBUILD_BUILTIN := 1

# The following temporary rule will make sure that people's
# trees get updated to the right permissions, since patch(1)
# can't do it
# ---------------------------------------------------------------------------

host-progs := fixdep split-include conmakehash docproc

include $(TOPDIR)/Rules.make


# ---------------------------------------------------------------------------
# Targets hardcoded and wellknow in top-level makefile
.PHONY: lxdialog
lxdialog:
	$(call descend,scripts/lxdialog,)

# fixdep is needed to compile other host programs
$(obj)/split-include $(obj)/docproc \
$(obj)/conmakehash lxdialog: $(obj)/fixdep
