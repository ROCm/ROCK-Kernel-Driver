###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# fix-dep: 	 Used to generate dependency information during build process
# split-include: Divide all config symbols up in a number of files in
#                include/config/...
# docproc: 	 Preprocess .tmpl file in order to generate .sgml documentation
# conmakehash:	 Create arrays for initializing the kernel console tables

host-progs    := fixdep split-include conmakehash docproc kallsyms
build-targets := $(host-progs)

# Let clean descend into subdirs
subdir-	:= lxdialog kconfig

# fixdep is needed to compile other host programs
$(addprefix $(obj)/,$(filter-out fixdep,$(host-progs))): $(obj)/fixdep
