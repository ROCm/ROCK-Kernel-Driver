###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# docproc: 	 Preprocess .tmpl file in order to generate .sgml docs
# conmakehash:	 Create arrays for initializing the kernel console tables

host-progs	:= conmakehash kallsyms pnmtologo bin2c
always		:= $(host-progs)

subdir-$(CONFIG_MODVERSIONS)	+= genksyms
subdir-y	+= mod

# Let clean descend into subdirs
subdir-	+= basic lxdialog kconfig package
