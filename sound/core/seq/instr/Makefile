#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := instr.o

list-multi   := snd-ainstr-fm.o snd-ainstr-simple.o snd-ainstr-gf1.o \
		snd-ainstr-iw.o

export-objs  := ainstr_fm.o ainstr_simple.o ainstr_gf1.o ainstr_iw.o

snd-ainstr-fm-objs := ainstr_fm.o
snd-ainstr-simple-objs := ainstr_simple.o
snd-ainstr-gf1-objs := ainstr_gf1.o
snd-ainstr-iw-objs := ainstr_iw.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ALS100) += snd-ainstr-fm.o
obj-$(CONFIG_SND_AZT2320) += snd-ainstr-fm.o
obj-$(CONFIG_SND_DT0197H) += snd-ainstr-fm.o
obj-$(CONFIG_SND_ES18XX) += snd-ainstr-fm.o
obj-$(CONFIG_SND_OPL3SA2) += snd-ainstr-fm.o
obj-$(CONFIG_SND_AD1816A) += snd-ainstr-fm.o
obj-$(CONFIG_SND_CS4232) += snd-ainstr-fm.o
obj-$(CONFIG_SND_CS4236) += snd-ainstr-fm.o
obj-$(CONFIG_SND_ES1688) += snd-ainstr-fm.o
obj-$(CONFIG_SND_GUSCLASSIC) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(CONFIG_SND_GUSMAX) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o snd-ainstr-fm.o
obj-$(CONFIG_SND_INTERWAVE) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-ainstr-iw.o snd-ainstr-gf1.o snd-ainstr-simple.o
obj-$(CONFIG_SND_OPTI92X_AD1848) += snd-ainstr-fm.o
obj-$(CONFIG_SND_OPTI92X_CS4231) += snd-ainstr-fm.o
obj-$(CONFIG_SND_OPTI93X) += snd-ainstr-fm.o
obj-$(CONFIG_SND_SB) += snd-ainstr-fm.o
obj-$(CONFIG_SND_SB16) += snd-ainstr-fm.o
obj-$(CONFIG_SND_SBAWE) += snd-ainstr-fm.o
obj-$(CONFIG_SND_WAVEFRONT) += snd-ainstr-fm.o
obj-$(CONFIG_SND_ALS4000) += snd-ainstr-fm.o
obj-$(CONFIG_SND_CMIPCI) += snd-ainstr-fm.o
obj-$(CONFIG_SND_CS4281) += snd-ainstr-fm.o
obj-$(CONFIG_SND_ES1938) += snd-ainstr-fm.o
obj-$(CONFIG_SND_FM801) += snd-ainstr-fm.o
obj-$(CONFIG_SND_SONICVIBES) += snd-ainstr-fm.o
obj-$(CONFIG_SND_TRIDENT) += snd-ainstr-simple.o

include $(TOPDIR)/Rules.make

snd-ainstr-fm.o: $(snd-ainstr-fm-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ainstr-fm-objs)

snd-ainstr-simple.o: $(snd-ainstr-simple-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ainstr-simple-objs)

snd-ainstr-gf1.o: $(snd-ainstr-gf1-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ainstr-gf1-objs)

snd-ainstr-iw.o: $(snd-ainstr-iw-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-ainstr-iw-objs)
