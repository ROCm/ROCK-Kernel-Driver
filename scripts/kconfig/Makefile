#################
#
# Shared Makefile for the various lkc executables:
# conf:	  Used for defconfig, oldconfig and related targets
# mconf:  Used for the mconfig target.
#         Utilizes the lxdialog package
# qconf:  Used for the xconfig target
#         Based on QT which needs to be installed to compile it
# gconf:  Used for the gconfig target
#         Based on GTK which needs to be installed to compile it
#
#################

# object files used by all lkc flavours
libkconfig-objs := zconf.tab.o

host-progs	:= conf mconf qconf gconf
conf-objs	:= conf.o  libkconfig.so
mconf-objs	:= mconf.o libkconfig.so

ifeq ($(MAKECMDGOALS),$(obj)/qconf)
qconf-cxxobjs	:= qconf.o
qconf-objs	:= kconfig_load.o
endif

ifeq ($(MAKECMDGOALS),$(obj)/gconf)
gconf-objs	:= gconf.o kconfig_load.o
endif

clean-files	:= libkconfig.so lkc_defs.h qconf.moc .tmp_qtcheck \
		   .tmp_gtkcheck zconf.tab.c zconf.tab.h lex.zconf.c

# generated files seem to need this to find local include files
HOSTCFLAGS_lex.zconf.o	:= -I$(src)
HOSTCFLAGS_zconf.tab.o	:= -I$(src)

HOSTLOADLIBES_qconf	= -L$(QTDIR)/lib -Wl,-rpath,$(QTDIR)/lib -l$(QTLIB) -ldl
HOSTCXXFLAGS_qconf.o	= -I$(QTDIR)/include 

HOSTLOADLIBES_gconf	= `pkg-config gtk+-2.0 gmodule-2.0 libglade-2.0 --libs`
HOSTCFLAGS_gconf.o	= `pkg-config gtk+-2.0 gmodule-2.0 libglade-2.0 --cflags`

$(obj)/conf.o $(obj)/mconf.o $(obj)/qconf.o $(obj)/gconf.o: $(obj)/zconf.tab.h

$(obj)/qconf.o: $(obj)/.tmp_qtcheck

ifeq ($(MAKECMDGOALS),$(obj)/qconf)
MOC = $(QTDIR)/bin/moc
-include $(obj)/.tmp_qtcheck

# QT needs some extra effort...
$(obj)/.tmp_qtcheck:
	@set -e; for d in $$QTDIR /usr/share/qt* /usr/lib/qt*; do \
	  if [ -f $$d/include/qconfig.h ]; then DIR=$$d; break; fi; \
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
	echo "QTDIR=$$DIR" > $@; echo "QTLIB=$$LIB" >> $@; \
	if [ ! -x $$DIR/bin/moc -a -x /usr/bin/moc ]; then \
	  echo "*"; \
	  echo "* Unable to find $$DIR/bin/moc, using /usr/bin/moc instead."; \
	  echo "*"; \
	  echo "MOC=/usr/bin/moc" >> $@; \
	fi
endif

$(obj)/gconf.o: $(obj)/.tmp_gtkcheck

ifeq ($(MAKECMDGOALS),$(obj)/gconf)
-include $(obj)/.tmp_gtkcheck

# GTK needs some extra effort, too...
$(obj)/.tmp_gtkcheck:
	@if `pkg-config gtk+-2.0 gmodule-2.0 libglade-2.0 --exists`; then		\
		if `pkg-config gtk+-2.0 --atleast-version=2.0.0`; then			\
			touch $@;								\
		else									\
			echo "*"; 							\
			echo "* GTK+ is present but version >= 2.0.0 is required.";	\
			echo "*";							\
			false;								\
		fi									\
	else										\
		echo "*"; 								\
		echo "* Unable to find the GTK+ installation. Please make sure that"; 	\
		echo "* the GTK+ 2.0 development package is correctly installed..."; 	\
		echo "* You need gtk+-2.0, glib-2.0 and libglade-2.0."; 		\
		echo "*"; 								\
		false;									\
	fi
endif

$(obj)/zconf.tab.o: $(obj)/lex.zconf.c

$(obj)/kconfig_load.o: $(obj)/lkc_defs.h

$(obj)/qconf.o: $(obj)/qconf.moc $(obj)/lkc_defs.h

$(obj)/gconf.o: $(obj)/lkc_defs.h

$(obj)/%.moc: $(src)/%.h
	$(MOC) -i $< -o $@

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
