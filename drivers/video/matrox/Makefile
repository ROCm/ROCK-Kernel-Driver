# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@edgeglobal.com>
# Rewritten to use lists instead of if-statements.

O_TARGET := matrox.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    :=  matroxfb_base.o matroxfb_accel.o matroxfb_DAC1064.o matroxfb_Ti3026.o matroxfb_misc.o

# Each configuration option enables a list of files.

obj-$(CONFIG_FB_MATROX)           += matroxfb_base.o matroxfb_accel.o matroxfb_DAC1064.o matroxfb_Ti3026.o matroxfb_misc.o
obj-$(CONFIG_FB_MATROX_I2C)       += i2c-matroxfb.o
obj-$(CONFIG_FB_MATROX_MAVEN)     += matroxfb_maven.o matroxfb_crtc2.o
obj-$(CONFIG_FB_MATROX_G450)	  += matroxfb_g450.o matroxfb_crtc2.o

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
