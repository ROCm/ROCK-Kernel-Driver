BOOKS	:= wanbook.sgml z8530book.sgml mcabook.sgml videobook.sgml \
	   kernel-api.sgml parportbook.sgml kernel-hacking.sgml \
	   kernel-locking.sgml via-audio.sgml mousedrivers.sgml sis900.sgml

PS	:=	$(patsubst %.sgml, %.ps, $(BOOKS))
PDF	:=	$(patsubst %.sgml, %.pdf, $(BOOKS))
HTML    :=      $(patsubst %.sgml, %, $(BOOKS))
IMG-parportbook := parport-share.fig parport-multi.fig parport-structure.fig
EPS-parportbook := $(patsubst %.fig, %.eps, $(IMG-parportbook))
JPG-parportbook := $(patsubst %.fig, %.jpeg, $(IMG-parportbook))

$(BOOKS): $(TOPDIR)/scripts/docproc

.PHONY:	books ps pdf html clean mrproper

books:	$(BOOKS)

ps:	$(PS)

pdf:	$(PDF)

html:	$(HTML)

%.eps: %.fig
	-fig2dev -Leps $< $@

%.jpeg: %.fig
	-fig2dev -Ljpeg $< $@

$(TOPDIR)/scripts/docproc:
	$(MAKE) -C $(TOPDIR)/scripts docproc

mousedrivers.sgml: mousedrivers.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

kernel-hacking.sgml: kernel-hacking.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

kernel-locking.sgml: kernel-locking.tmpl
	$(TOPDIR)/scripts/docgen <$< >$@

wanbook.sgml: wanbook.tmpl $(TOPDIR)/drivers/net/wan/syncppp.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/syncppp.c \
		<wanbook.tmpl >wanbook.sgml

z8530book.sgml: z8530book.tmpl $(TOPDIR)/drivers/net/wan/z85230.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/wan/z85230.c \
		<z8530book.tmpl >z8530book.sgml

via-audio.sgml: via-audio.tmpl $(TOPDIR)/drivers/sound/via82cxxx_audio.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/sound/via82cxxx_audio.c \
		<via-audio.tmpl >via-audio.sgml

sis900.sgml: sis900.tmpl $(TOPDIR)/drivers/net/sis900.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/net/sis900.c \
		<sis900.tmpl >sis900.sgml

mcabook.sgml: mcabook.tmpl $(TOPDIR)/arch/i386/kernel/mca.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/arch/i386/kernel/mca.c \
		<mcabook.tmpl >mcabook.sgml

videobook.sgml: videobook.tmpl $(TOPDIR)/drivers/media/video/videodev.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/media/video/videodev.c \
		<videobook.tmpl >videobook.sgml

APISOURCES :=	$(TOPDIR)/drivers/media/video/videodev.c \
		$(TOPDIR)/arch/i386/kernel/mca.c \
		$(TOPDIR)/arch/i386/kernel/mtrr.c \
		$(TOPDIR)/drivers/char/misc.c \
		$(TOPDIR)/drivers/net/net_init.c \
		$(TOPDIR)/drivers/net/8390.c \
		$(TOPDIR)/drivers/char/serial.c \
		$(TOPDIR)/drivers/pci/pci.c \
		$(TOPDIR)/drivers/block/ll_rw_blk.c \
		$(TOPDIR)/drivers/sound/sound_core.c \
		$(TOPDIR)/drivers/sound/sound_firmware.c \
		$(TOPDIR)/drivers/net/wan/syncppp.c \
		$(TOPDIR)/drivers/net/wan/z85230.c \
		$(TOPDIR)/drivers/usb/usb.c \
		$(TOPDIR)/fs/locks.c \
		$(TOPDIR)/fs/devfs/base.c \
		$(TOPDIR)/kernel/pm.c \
		$(TOPDIR)/kernel/ksyms.c \
		$(TOPDIR)/net/netsyms.c
 
kernel-api.sgml: kernel-api.tmpl $(APISOURCES)
	$(TOPDIR)/scripts/docgen $(APISOURCES) \
		<kernel-api.tmpl >kernel-api.sgml

kernel-api-man: $(APISOURCES)
	@rm -rf $(TOPDIR)/Documentation/man
	$(TOPDIR)/scripts/kernel-doc -man $^ | \
		$(PERL) $(TOPDIR)/scripts/split-man $(TOPDIR)/Documentation/man

parportbook: $(JPG-parportbook)
parportbook.ps: $(EPS-parportbook)
parportbook.sgml: parportbook.tmpl $(TOPDIR)/drivers/parport/init.c
	$(TOPDIR)/scripts/docgen $(TOPDIR)/drivers/parport/init.c <$< >$@

DVI	:=	$(patsubst %.sgml, %.dvi, $(BOOKS))
AUX	:=	$(patsubst %.sgml, %.aux, $(BOOKS))
TEX	:=	$(patsubst %.sgml, %.tex, $(BOOKS))
LOG	:=	$(patsubst %.sgml, %.log, $(BOOKS))
OUT	:=	$(patsubst %.sgml, %.out, $(BOOKS))

clean:
	-$(RM) core *~
	-$(RM) $(BOOKS)
	-$(RM) $(DVI) $(AUX) $(TEX) $(LOG) $(OUT)
	-$(RM) $(JPG-parportbook) $(EPS-parportbook)

mrproper: clean
	-$(RM) $(PS) $(PDF)
	-$(RM) -r $(HTML)
	-$(RM) .depend
	-$(RM) $(TOPDIR)/scripts/mkdep-docbook

%.ps : %.sgml
	@(which db2ps > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	db2ps $<

%.pdf : %.sgml
	@(which db2pdf > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	db2pdf $<

%:	%.sgml
	@(which db2html > /dev/null 2>&1) || \
	 (echo "*** You need to install DocBook stylesheets ***"; \
	  exit 1)
	-$(RM) -r $@
	db2html $<
	if [ ! -z "$(JPG-$@)" ]; then cp $(JPG-$@) $@; fi

#
# we could have our own dependency generator
#
#
# .depend: $(TOPDIR)/scripts/mkdep-docbook
#	$(TOPDIR)/scripts/mkdep-docbook $(wildcard *.tmpl) > .depend

include $(TOPDIR)/Rules.make

