###
# scripts contains sources for various helper programs used throughout
# the kernel for the build process.
# ---------------------------------------------------------------------------
# fix-dep: 	 Used to generate dependency information during build process
# split-include: Divide all config symbols up in a number of files in
#                include/config/...
# docproc: 	 Preprocess .tmpl file in order to generate .sgml documentation
# conmakehash:	 Create arrays for initializing the kernel console tables
# tkparse: 	 Used by xconfig

EXTRA_TARGETS := fixdep split-include docproc conmakehash

# Yikes. We need to build this stuff here even if the user only wants
# modules.

KBUILD_BUILTIN := 1

# The following temporary rule will make sure that people's
# trees get updated to the right permissions, since patch(1)
# can't do it
# ---------------------------------------------------------------------------

host-progs := fixdep split-include conmakehash docproc tkparse
tkparse-objs := tkparse.o tkcond.o tkgen.o

include $(TOPDIR)/Rules.make

# In reality kconfig.tk should depend on all Config.in files,
# but it is not worth the effort to generate the dependencies.
# The alternative solution to always generate it is fairly fast.
# FORCE it to remake
$(obj)/kconfig.tk: $(srctree)/arch/$(ARCH)/config.in $(obj)/tkparse FORCE
	@echo '  Generating $@'
	@(                                                      \
	if [ -f /usr/local/bin/wish ];        then              \
		echo '#!'"/usr/local/bin/wish -f";              \
	else                                                    \
		echo '#!'"/usr/bin/wish -f";                    \
	fi;                                                     \
	cat $(src)/header.tk;                                   \
	$(obj)/tkparse < $<;                                    \
	echo "set defaults \"arch/${ARCH}/defconfig\"";         \
	echo "set ARCH \"${ARCH}\"";                            \
	cat $(src)/tail.tk;                                     \
	) > $@
	@chmod 755 $@


# ---------------------------------------------------------------------------
# Targets hardcoded and wellknow in top-level makefile
.PHONY: lxdialog
lxdialog:
	$(call descend,lxdialog,)

# fixdep is needed to compile other host programs
$(obj)/split-include $(obj)/docproc $(addprefix $(obj)/,$(tkparse-objs)) \
$(obj)/conmakehash lxdialog: $(obj)/fixdep

mrproper:
	@echo 'Making mrproper (scripts)'
	@rm -f $(tkparse-objs) $(obj)/kconfig.tk
	@rm -f core $(host-progs)
	@$(MAKE) -C lxdialog mrproper

