# Makefile for the Linux graphics to console drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@users.sf.net>
# Rewritten to use lists instead of if-statements.

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    	:= fbcon.o fbcon-accel.o fbcon-afb.o fbcon-ilbm.o \
		   fbcon-iplan2p2.o fbcon-iplan2p4.o fbcon-iplan2p8.o \
		   fbcon-vga-planes.o fbcon-vga8-planes.o fbcon-hga.o

# Each configuration option enables a list of files.

obj-$(CONFIG_DUMMY_CONSOLE)       += dummycon.o
obj-$(CONFIG_SGI_NEWPORT_CONSOLE) += newport_con.o
obj-$(CONFIG_PROM_CONSOLE)        += promcon.o promcon_tbl.o
obj-$(CONFIG_STI_CONSOLE)         += sticon.o sticon-bmode.o ../sticore.o
obj-$(CONFIG_VGA_CONSOLE)         += vgacon.o
obj-$(CONFIG_MDA_CONSOLE)         += mdacon.o

obj-$(CONFIG_FONT_SUN8x16)        += font_sun8x16.o
obj-$(CONFIG_FONT_SUN12x22)       += font_sun12x22.o
obj-$(CONFIG_FONT_8x8)            += font_8x8.o
obj-$(CONFIG_FONT_8x16)           += font_8x16.o
obj-$(CONFIG_FONT_6x11)           += font_6x11.o
obj-$(CONFIG_FONT_PEARL_8x8)      += font_pearl_8x8.o
obj-$(CONFIG_FONT_ACORN_8x8)      += font_acorn_8x8.o
obj-$(CONFIG_FONT_MINI_4x6)       += font_mini_4x6.o

obj-$(CONFIG_FRAMEBUFFER_CONSOLE) += fbcon.o fonts.o

# Generic Low Level Drivers

obj-$(CONFIG_FBCON_AFB)           += fbcon-afb.o
obj-$(CONFIG_FBCON_ILBM)          += fbcon-ilbm.o
obj-$(CONFIG_FBCON_IPLAN2P2)      += fbcon-iplan2p2.o
obj-$(CONFIG_FBCON_IPLAN2P4)      += fbcon-iplan2p4.o
obj-$(CONFIG_FBCON_IPLAN2P8)      += fbcon-iplan2p8.o
obj-$(CONFIG_FBCON_HGA)           += fbcon-hga.o
obj-$(CONFIG_FBCON_STI)           += fbcon-sti.o
obj-$(CONFIG_FBCON_ACCEL)	  += fbcon-accel.o

include $(TOPDIR)/Rules.make

$(obj)/promcon_tbl.c: $(src)/prom.uni
	$(objtree)/scripts/conmakehash $< | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > $@

