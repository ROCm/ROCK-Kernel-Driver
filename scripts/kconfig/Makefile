#################
#
# Shared Makefile for the various lkc executables:
# conf:	  Used for defconfig, oldconfig and related targets
# mconf:  Used for the mconfig target.
#         Utilizes the lxdialog package
# qconf:  Used for the xconfig target
#         Based on QT which needs to be installed to compile it
#

# object files used by all lkc flavours
libkconfig-objs := zconf.tab.o

host-progs	:= conf mconf qconf
conf-objs	:= conf.o  libkconfig.so
mconf-objs	:= mconf.o libkconfig.so

qconf-objs	:= kconfig_load.o
qconf-cxxobjs	:= qconf.o

clean-files	:= libkconfig.so lkc_defs.h qconf.moc .tmp_qtcheck \
		   zconf.tab.c zconf.tab.h lex.zconf.c

include $(TOPDIR)/Rules.make

# generated files seem to need this to find local include files
HOSTCFLAGS_lex.zconf.o	:= -I$(src)
HOSTCFLAGS_zconf.tab.o	:= -I$(src)

HOSTLOADLIBES_qconf	= -L$(QTDIR)/lib -Wl,-rpath,$(QTDIR)/lib -l$(QTLIB) -ldl
HOSTCXXFLAGS_qconf.o	= -I$(QTDIR)/include 

$(obj)/conf.o $(obj)/mconf.o $(obj)/qconf.o: $(obj)/zconf.tab.h

$(obj)/qconf.o: $(obj)/.tmp_qtcheck

-include $(obj)/.tmp_qtcheck

# QT needs some extra effort...
$(obj)/.tmp_qtcheck:
	@set -e; for d in $$QTDIR /usr/share/qt /usr/lib/qt3; do \
	  if [ -x $$d/bin/moc ]; then DIR=$$d; break; fi; \
	done; \
	if [ -z "$$DIR" ]; then \
	  echo "*"; \
	  echo "* Unable to find the QT installation. Please make sure that the"; \
	  echo "* QT development package is correctly installed and the QTDIR"; \
	  echo "* environment variable is set to the correct location."; \
	  echo "*"; \
	  false; \
	fi; \
	LIB=qt; \
	if [ -f $$DIR/lib/libqt-mt.so ]; then LIB=qt-mt; fi; \
	echo "QTDIR=$$DIR" > $@; echo "QTLIB=$$LIB" >> $@

$(obj)/zconf.tab.o: $(obj)/lex.zconf.c

$(obj)/kconfig_load.o: $(obj)/lkc_defs.h

$(obj)/qconf.o: $(obj)/qconf.moc $(obj)/lkc_defs.h

$(obj)/%.moc: $(src)/%.h
	$(QTDIR)/bin/moc -i $< -o $@

$(obj)/lkc_defs.h: $(src)/lkc_proto.h
	sed < $< > $@ 's/P(\([^,]*\),.*/#define \1 (\*\1_p)/'


###
# The following requires flex/bison
# By default we use the _shipped versions, uncomment the following line if
# you are modifying the flex/bison src.
# LKC_GENPARSER := 1

ifdef LKC_GENPARSER

$(obj)/zconf.tab.c: $(obj)/zconf.y 
$(obj)/zconf.tab.h: $(obj)/zconf.tab.c

%.tab.c: %.y
	bison -t -d -v -b $* -p $(notdir $*) $<

lex.%.c: %.l
	flex -P$(notdir $*) -o$@ $<

endif
