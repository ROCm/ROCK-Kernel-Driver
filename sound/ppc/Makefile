#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
#

snd-mpc52xx-ac97-objs := mpc52xx_ac97.o
obj-$(CONFIG_SND_PPC_MPC52xx_AC97)	+= snd-mpc52xx-ac97.o
snd-powermac-objs := powermac.o pmac.o awacs.o burgundy.o daca.o tumbler.o keywest.o beep.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_POWERMAC)	+= snd-powermac.o
obj-$(CONFIG_SND_PS3)		+= snd_ps3.o
