###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# fix-dep: 	 Used to generate dependency information during build process
# split-include: Divide all config symbols up in a number of files in
#                include/config/...
# docproc: 	 Preprocess .tmpl file in order to generate .sgml docs
# conmakehash:	 Create arrays for initializing the kernel console tables

host-progs	:= fixdep split-include conmakehash docproc kallsyms modpost \
		   mk_elfconfig pnmtologo bin2c
always		:= $(host-progs) empty.o

modpost-objs	:= modpost.o file2alias.o

subdir-$(CONFIG_MODVERSIONS)	+= genksyms

# Let clean descend into subdirs
subdir-	+= lxdialog kconfig

# fixdep is needed to compile other host programs
$(addprefix $(obj)/,$(filter-out fixdep,$(always)) $(subdir-y)): $(obj)/fixdep

# dependencies on generated files need to be listed explicitly

$(obj)/modpost.o $(obj)/file2alias.o: $(obj)/elfconfig.h

quiet_cmd_elfconfig = MKELF   $@
      cmd_elfconfig = $(obj)/mk_elfconfig $(ARCH) < $< > $@

$(obj)/elfconfig.h: $(obj)/empty.o $(obj)/mk_elfconfig FORCE
	$(call if_changed,elfconfig)

targets += elfconfig.h
