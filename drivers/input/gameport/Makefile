#
# Makefile for the gameport drivers.
#

# Objects that export symbols.

export-objs	:= gameport.o

# Each configuration option enables a list of files.

obj-$(CONFIG_GAMEPORT)		+= gameport.o
obj-$(CONFIG_GAMEPORT_CS461X)	+= cs461x.o
obj-$(CONFIG_GAMEPORT_EMU10K1)	+= emu10k1-gp.o
obj-$(CONFIG_GAMEPORT_FM801)	+= fm801-gp.o
obj-$(CONFIG_GAMEPORT_L4)	+= lightning.o
obj-$(CONFIG_GAMEPORT_NS558)	+= ns558.o
obj-$(CONFIG_GAMEPORT_VORTEX)	+= vortex.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
