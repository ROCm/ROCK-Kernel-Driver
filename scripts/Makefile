###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# docproc: 	 Preprocess .tmpl file in order to generate .sgml docs
# conmakehash:	 Create arrays for initializing the kernel console tables

host-progs	:= conmakehash kallsyms modpost mk_elfconfig pnmtologo bin2c
always		:= $(host-progs) empty.o

modpost-objs	:= modpost.o file2alias.o sumversion.o

subdir-$(CONFIG_MODVERSIONS)	+= genksyms

# Let clean descend into subdirs
subdir-	+= basic lxdialog kconfig package

# dependencies on generated files need to be listed explicitly

$(obj)/modpost.o $(obj)/file2alias.o $(obj)/sumversion.o: $(obj)/elfconfig.h

quiet_cmd_elfconfig = MKELF   $@
      cmd_elfconfig = $(obj)/mk_elfconfig $(ARCH) < $< > $@

$(obj)/elfconfig.h: $(obj)/empty.o $(obj)/mk_elfconfig FORCE
	$(call if_changed,elfconfig)

targets += elfconfig.h
