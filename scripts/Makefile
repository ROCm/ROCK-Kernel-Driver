
# always needed
# ---------------------------------------------------------------------------

# The following temporary rule will make sure that people's
# trees get updated to the right permissions, since patch(1)
# can't do it
CHMOD_FILES := docgen gen-all-syms kernel-doc mkcompile_h makelst

all: fixdep split-include $(CHMOD_FILES)

$(CHMOD_FILES): FORCE
	@chmod a+x $@

# xconfig
# ---------------------------------------------------------------------------

HEADER=header.tk
TAIL=tail.tk
# Previous versions always remade kconfig.tk because they always depended
# on soundscript.  This runs fairly fast, and I can't find all the
# Config.in files to depend on anyways.  So I'll force it to remake.

kconfig.tk: $(TOPDIR)/arch/$(ARCH)/config.in tkparse $(HEADER) $(TAIL) FORCE
	@echo Generating $@
	@(                                                      \
	  if [ -f /usr/local/bin/wish ];	then            \
		echo '#!'"/usr/local/bin/wish -f";              \
	  else                                                  \
		echo '#!'"/usr/bin/wish -f";                    \
	  fi;                                                   \
	  cat $(HEADER);                                        \
	  ./tkparse < $<;                                       \
	  echo "set defaults \"arch/${ARCH}/defconfig\"";       \
	  echo "set ARCH \"${ARCH}\"";                          \
	  cat $(TAIL)                                           \
	) > $@
	chmod 755 $@

# DocBook stuff
# ---------------------------------------------------------------------------

doc-progs: docproc

# ---------------------------------------------------------------------------

.PHONY: lxdialog

lxdialog:
	$(MAKE) -C lxdialog all

# ---------------------------------------------------------------------------

host-progs := fixdep split-include docproc tkparse

tkparse-objs := tkparse.o tkcond.o tkgen.o

# fixdep is needed to compile other host programs

split-include docproc $(tkparse-objs) lxdialog: fixdep


include $(TOPDIR)/Rules.make
