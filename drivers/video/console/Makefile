# Makefile for the Linux graphics to console drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@users.sf.net>
# Rewritten to use lists instead of if-statements.

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    	:= fbcon.o fonts.o

# Font handling
font-objs := fonts.o

ifeq ($(CONFIG_FONT_SUN8x16),y)
	font-objs += font_sun8x16.o
endif
ifeq ($(CONFIG_FONT_SUN12x22),y)
	font-objs += font_sun12x22.o
endif	
ifeq ($(CONFIG_FONT_8x8),y)
	font-objs += font_8x8.o
endif	
ifeq ($(CONFIG_FONT_8x16),y)
	font-objs += font_8x16.o
endif	
ifeq ($(CONFIG_FONT_6x11),y)
	font-objs += font_6x11.o
endif	
ifeq ($(CONFIG_FONT_PEARL_8x8),y)
	font-objs += font_pearl_8x8.o
endif	
ifeq ($(CONFIG_FONT_ACORN_8x8),y)
	font-objs += font_acorn_8x8.o
endif	
ifeq ($(CONFIG_FONT_MINI_4x6),y)
	font-objs += font_mini_4x6.o
endif	

# Each configuration option enables a list of files.

obj-$(CONFIG_DUMMY_CONSOLE)       += dummycon.o
obj-$(CONFIG_SGI_NEWPORT_CONSOLE) += newport_con.o
obj-$(CONFIG_PROM_CONSOLE)        += promcon.o promcon_tbl.o
obj-$(CONFIG_STI_CONSOLE)         += sticon.o sticore.o
obj-$(CONFIG_VGA_CONSOLE)         += vgacon.o
obj-$(CONFIG_MDA_CONSOLE)         += mdacon.o
obj-$(CONFIG_FRAMEBUFFER_CONSOLE) += fbcon.o font.o

obj-$(CONFIG_FB_STI)              += sticore.o

# Files generated that shall be removed upon make clean
clean-files := promcon_tbl.c

include $(TOPDIR)/Rules.make

$(obj)/promcon_tbl.c: $(src)/prom.uni
	$(objtree)/scripts/conmakehash $< | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > $@

