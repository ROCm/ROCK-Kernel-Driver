###
# This makefile is used to generate the kernel documentation,
# primarily based on in-line comments in various source files.
# See Documentation/kernel-doc-nano-HOWTO.txt for instruction in how
# to ducument the SRC - and how to read it.
# To add a new book the only step required is to add the book to the
# list of DOCBOOKS.

DOCBOOKS := wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml \
	    parportbook.sgml kernel-hacking.sgml \
	    kernel-locking.sgml via-audio.sgml mousedrivers.sgml \
	    deviceiobook.sgml procfs-guide.sgml tulip-user.sgml \
	    writing_usb_driver.sgml scsidrivers.sgml sis900.sgml \
	    kernel-api.sgml journal-api.sgml lsm.sgml usb.sgml \
	    gadget.sgml

###
# The build process is as follows (targets):
#              (sgmldocs)
# file.tmpl --> file.sgml +--> file.ps  (psdocs)
#                         +--> file.pdf  (pdfdocs)
#                         +--> DIR=file  (htmldocs)

###
# The targets that may be used.
.PHONY:	sgmldocs psdocs pdfdocs htmldocs clean mrproper

BOOKS := $(addprefix $(obj)/,$(DOCBOOKS))
sgmldocs: $(BOOKS)

PS := $(patsubst %.sgml, %.ps, $(BOOKS))
psdocs: $(PS)

PDF := $(patsubst %.sgml, %.pdf, $(BOOKS))
pdfdocs: $(PDF)

HTML := $(patsubst %.sgml, %.html, $(BOOKS))
htmldocs: $(HTML)

###
#External programs used
KERNELDOC=$(objtree)/scripts/kernel-doc
DOCPROC=$(objtree)/scripts/docproc

###
# DOCPROC is used for two purposes:
# 1) To generate a dependency list for a .tmpl file
# 2) To preprocess a .tmpl file and call kernel-doc with
#     appropriate parameters.
# The following rules are used to generate the .sgml documentation
# required to generate the final targets. (ps, pdf, html).
quiet_cmd_docproc = DOCPROC $@
cmd_docproc = $(DOCPROC) doc $< >$@
define rule_docproc
	set -e
        $(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';) 
        $(cmd_$(1)); \
        ( \
          echo 'cmd_$@ := $(cmd_$(1))'; \
          echo $@: `$(DOCPROC) depend $<`; \
        ) > $(dir $@).$(notdir $@).cmd
endef

%.sgml: %.tmpl FORCE
	$(call if_changed_rule,docproc)

###
#Read in all saved dependency files 
cmd_files := $(wildcard $(foreach f,$(BOOKS),$(dir $(f)).$(notdir $(f)).cmd))

ifneq ($(cmd_files),)
  include $(cmd_files)
endif

###
# Changes in kernel-doc force a rebuild of all documentation
$(BOOKS): $(KERNELDOC)

###
# procfs guide uses a .c file as example code.
# This requires an explicit dependency
C-procfs-example = procfs_example.sgml
C-procfs-example2 = $(addprefix $(obj)/,$(C-procfs-example))
$(obj)/procfs-guide.sgml: $(C-procfs-example2)

###
# The parportbook includes a few images.
# Force them to be build before the books 
IMG-parportbook := parport-share.fig parport-multi.fig parport-structure.fig
IMG-parportbook2 := $(addprefix $(obj)/,$(IMG-parportbook))
EPS-parportbook := $(patsubst %.fig,%.eps, $(IMG-parportbook2))
PNG-parportbook := $(patsubst %.fig,%.png, $(IMG-parportbook2))
$(obj)/parportbook.html: $(PNG-parportbook)
$(obj)/parportbook.ps $(obj)/parportbook.pdf: $(EPS-parportbook) $(PNG-parportbook)

###
# Rules to generate postscript, PDF and HTML
# db2html creates a directory. Generate a html file used for timestamp
%.ps : %.sgml
	@(which db2ps > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	$(call do_cmd,DB2PS   $@,db2ps -o $(dir $@) $<)

%.pdf : %.sgml
	@(which db2pdf > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	$(call do_cmd,DB2PDF  $@,db2pdf -o $(dir $@) $<)

%.html:	%.sgml
	@(which db2html > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	@rm -rf $@ $(patsubst %.html,%,$@)
	$(call do_cmd,DB2HTML $@,db2html -o $(patsubst %.html,%,$@) $< && \
         echo '<a HREF="$(patsubst %.html,%,$(notdir $@))/book1.html">\
         Goto $(patsubst %.html,%,$(notdir $@))</a><p>' > $@)
	@if [ ! -z "$(PNG-$(basename $(notdir $@)))" ]; then \
            cp $(PNG-$(basename $(notdir $@))) $(patsubst %.html,%,$@); fi

###
# Rules to generate postscripts and PNG imgages from .fig format files
%.eps: %.fig
	$(call do_cmd,FIG2DEV -Leps $@,fig2dev -Leps $< $@)

%.png: %.fig
	$(call do_cmd,FIG2DEV -Lpng $@,fig2dev -Lpng $< $@)

###
# Rule to convert a .c file to inline SGML documentation
%.sgml: %.c
	@echo '  Generating $@'
	@(                            \
	   echo "<programlisting>";   \
	   expand --tabs=8 < $< |     \
	   sed -e "s/&/\\&amp;/g"     \
	       -e "s/</\\&lt;/g"      \
	       -e "s/>/\\&gt;/g";     \
	   echo "</programlisting>")  > $@

###
# Help targets as used by the top-level makefile
dochelp:
	@echo  '  Linux kernel internal documentation in different formats:'
	@echo  '  sgmldocs (SGML), psdocs (Postscript), pdfdocs (PDF), htmldocs (HTML)'

###
# clean and mrproper as used by the top-level makefile 
# Temporary files left by various tools
clean-files := $(DOCBOOKS) \
	$(patsubst %.sgml, %.dvi,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.aux,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.tex,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.log,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.out,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.ps,   $(DOCBOOKS)) \
	$(patsubst %.sgml, %.pdf,  $(DOCBOOKS)) \
	$(patsubst %.sgml, %.html, $(DOCBOOKS)) \
	$(patsubst %.fig,%.eps, $(IMG-parportbook)) \
	$(patsubst %.fig,%.png, $(IMG-parportbook)) \
	$(C-procfs-example)

ifneq ($(wildcard $(patsubst %.html,%,$(HTML))),)
clean-rule := rm -rf $(wildcard $(patsubst %.html,%,$(HTML)))
endif
