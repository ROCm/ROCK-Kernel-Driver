#
# Makefile for the gameport drivers.
#

# The target object and module list name.

O_TARGET	:= gamedrv.o

# Objects that export symbols.

export-objs	:= gameport.o

# Each configuration option enables a list of files.

obj-$(CONFIG_GAMEPORT)		+= gameport.o
obj-$(CONFIG_GAMEPORT_CS461X)	+= cs461x.o
obj-$(CONFIG_GAMEPORT_EMU10K1)	+= emu10k1-gp.o
obj-$(CONFIG_GAMEPORT_L4)	+= lightning.o
obj-$(CONFIG_GAMEPORT_NS558)	+= ns558.o
obj-$(CONFIG_GAMEPORT_PCIGAME)	+= pcigame.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
