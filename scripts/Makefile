
# always needed
# ---------------------------------------------------------------------------

# The following temporary rule will make sure that people's
# trees get updated to the right permissions, since patch(1)
# can't do it
CHMOD_FILES := docgen gen-all-syms kernel-doc mkcompile_h mkversion_h makelst

all: fixdep split-include $(CHMOD_FILES)

fixdep: fixdep.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

split-include: split-include.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

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

tkparse: tkparse.o tkcond.o tkgen.o
	$(HOSTCC) -o $@ $^

tkparse.o tkcond.o tkgen.o: %.o: %.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

# DocBook stuff
# ---------------------------------------------------------------------------

doc-progs: docproc

docproc: docproc.c
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $<

include $(TOPDIR)/Rules.make

# we don't have automatic deps for host programs

tkparse.o tkcond.o tkgen.o: tkparse.h

