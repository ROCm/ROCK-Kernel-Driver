#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := isa.o

subdir-y     := ad1816a ad1848 cs423x es1688 gus opti9xx sb wavefront
subdir-m     := $(subdir-y)

list-multi   := snd-als100.o snd-azt2320.o snd-cmi8330.o snd-dt0197h.o \
		snd-es18xx.o snd-opl3sa2.o snd-sgalaxy.o

snd-als100-objs := als100.o
snd-azt2320-objs := azt2320.o
snd-cmi8330-objs := cmi8330.o
snd-dt0197h-objs := dt0197h.o
snd-es18xx-objs := es18xx.o
snd-opl3sa2-objs := opl3sa2.o
snd-sgalaxy-objs := sgalaxy.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ALS100) += snd-als100.o
obj-$(CONFIG_SND_AZT2320) += snd-azt2320.o
obj-$(CONFIG_SND_CMI8330) += snd-cmi8330.o
obj-$(CONFIG_SND_DT0197H) += snd-dt0197h.o
obj-$(CONFIG_SND_ES18XX) += snd-es18xx.o
obj-$(CONFIG_SND_OPL3SA2) += snd-opl3sa2.o
obj-$(CONFIG_SND_SGALAXY) += snd-sgalaxy.o

include $(TOPDIR)/Rules.make

snd-als100.o: $(snd-als100-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-als100-objs)

snd-azt2320.o: $(snd-azt2320-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-azt2320-objs)

snd-cmi8330.o: $(snd-cmi8330-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-cmi8330-objs)

snd-dt0197h.o: $(snd-dt0197h-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-dt0197h-objs)

snd-es18xx.o: $(snd-es18xx-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-es18xx-objs)

snd-opl3sa2.o: $(snd-opl3sa2-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-opl3sa2-objs)

snd-sgalaxy.o: $(snd-sgalaxy-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-sgalaxy-objs)
