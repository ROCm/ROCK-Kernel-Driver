#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

O_TARGET     := _gus.o

list-multi   := snd-gus-lib.o snd-gusclassic.o snd-gusextreme.o \
		snd-gusmax.o snd-interwave.o snd-interwave-stb.o

export-objs  := gus_main.o gus_volume.o

snd-gus-lib-objs := gus_main.o \
		    gus_io.o gus_irq.o gus_timer.o \
		    gus_mem.o gus_mem_proc.o gus_dram.o gus_dma.o gus_volume.o \
		    gus_pcm.o gus_mixer.o \
		    gus_uart.o \
		    gus_reset.o
snd-gus-synth-objs := gus_synth.o gus_sample.o gus_simple.o gus_instr.o

snd-gusclassic-objs := gusclassic.o
snd-gusextreme-objs := gusextreme.o
snd-gusmax-objs := gusmax.o
snd-interwave-objs := interwave.o
snd-interwave-stb-objs := interwave-stb.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_GUSCLASSIC) += snd-gusclassic.o snd-gus-lib.o
obj-$(CONFIG_SND_GUSMAX) += snd-gusmax.o snd-gus-lib.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-gusextreme.o snd-gus-lib.o
obj-$(CONFIG_SND_INTERWAVE) += snd-interwave.o snd-gus-lib.o
obj-$(CONFIG_SND_INTERWAVE_STB) += snd-interwave-stb.o snd-gus-lib.o
ifeq ($(subst m,y,$(CONFIG_SND_SEQUENCER)),y)
  obj-$(CONFIG_SND_GUSCLASSIC) += snd-gus-synth.o
  obj-$(CONFIG_SND_GUSMAX) += snd-gus-synth.o
  obj-$(CONFIG_SND_GUSEXTREME) += snd-gus-synth.o
  obj-$(CONFIG_SND_INTERWAVE) += snd-gus-synth.o
  obj-$(CONFIG_SND_INTERWAVE_STB) += snd-gus-synth.o
endif

include $(TOPDIR)/Rules.make

snd-gus-lib.o: $(snd-gus-lib-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-gus-lib-objs)

snd-gus-synth.o: $(snd-gus-synth-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-gus-synth-objs)

snd-gusclassic.o: $(snd-gusclassic-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-gusclassic-objs)

snd-gusextreme.o: $(snd-gusextreme-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-gusextreme-objs)

snd-gusmax.o: $(snd-gusmax-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-gusmax-objs)

snd-interwave.o: $(snd-interwave-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-interwave-objs)

snd-interwave-stb.o: $(snd-interwave-stb-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(snd-interwave-stb-objs)
