# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@edgeglobal.com>
# Rewritten to use lists instead of if-statements.

O_TARGET := video.o

mod-subdirs	:= matrox

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    := fbmem.o fbcmap.o fbcon.o fbmon.o modedb.o \
		  fbcon-afb.o fbcon-ilbm.o \
		  fbcon-vga.o fbcon-iplan2p2.o fbcon-iplan2p4.o \
		  fbcon-iplan2p8.o fbcon-vga-planes.o fbcon-cfb16.o \
		  fbcon-cfb2.o fbcon-cfb24.o fbcon-cfb32.o fbcon-cfb4.o \
		  fbcon-cfb8.o fbcon-mac.o fbcon-mfb.o \
		  cyber2000fb.o fbcon-hga.o

# Each configuration option enables a list of files.

obj-$(CONFIG_DUMMY_CONSOLE)       += dummycon.o
obj-$(CONFIG_SGI_NEWPORT_CONSOLE) += newport_con.o
obj-$(CONFIG_PROM_CONSOLE)        += promcon.o promcon_tbl.o
obj-$(CONFIG_VGA_CONSOLE)         += vgacon.o
obj-$(CONFIG_MDA_CONSOLE)         += mdacon.o

obj-$(CONFIG_FONT_SUN8x16)        += font_sun8x16.o
obj-$(CONFIG_FONT_SUN12x22)       += font_sun12x22.o
obj-$(CONFIG_FONT_8x8)            += font_8x8.o
obj-$(CONFIG_FONT_8x16)           += font_8x16.o
obj-$(CONFIG_FONT_6x11)           += font_6x11.o
obj-$(CONFIG_FONT_PEARL_8x8)      += font_pearl_8x8.o
obj-$(CONFIG_FONT_ACORN_8x8)      += font_acorn_8x8.o

# Add fbmon.o back into obj-$(CONFIG_FB) in 2.5.x
obj-$(CONFIG_FB)                  += fbmem.o fbcmap.o modedb.o fbcon.o fonts.o
# Only include macmodes.o if we have FB support and are PPC
ifeq ($(CONFIG_FB),y)
obj-$(CONFIG_PPC)                 += macmodes.o
endif

obj-$(CONFIG_FB_ACORN)            += acornfb.o
obj-$(CONFIG_FB_AMIGA)            += amifb.o
obj-$(CONFIG_FB_PM2)              += pm2fb.o fbgen.o
obj-$(CONFIG_FB_APOLLO)           += dnfb.o
obj-$(CONFIG_FB_Q40)              += q40fb.o
obj-$(CONFIG_FB_ATARI)            += atafb.o
obj-$(CONFIG_FB_ATY)              += atyfb.o
obj-$(CONFIG_FB_ATY128)           += aty128fb.o
obj-$(CONFIG_FB_IGA)              += igafb.o
obj-$(CONFIG_FB_CONTROL)          += controlfb.o
obj-$(CONFIG_FB_PLATINUM)         += platinumfb.o
obj-$(CONFIG_FB_VALKYRIE)         += valkyriefb.o
obj-$(CONFIG_FB_CT65550)          += chipsfb.o
obj-$(CONFIG_FB_CYBER)            += cyberfb.o
obj-$(CONFIG_FB_CYBER2000)        += cyber2000fb.o
obj-$(CONFIG_FB_SGIVW)            += sgivwfb.o
obj-$(CONFIG_FB_3DFX)             += tdfxfb.o
obj-$(CONFIG_FB_MAC)              += macfb.o macmodes.o
obj-$(CONFIG_FB_HP300)            += hpfb.o
obj-$(CONFIG_FB_OF)               += offb.o
obj-$(CONFIG_FB_IMSTT)            += imsttfb.o
obj-$(CONFIG_FB_RETINAZ3)         += retz3fb.o
obj-$(CONFIG_FB_CLGEN)            += clgenfb.o fbgen.o
obj-$(CONFIG_FB_S3TRIO)           += S3triofb.o
obj-$(CONFIG_FB_TGA)              += tgafb.o fbgen.o
obj-$(CONFIG_FB_VESA)             += vesafb.o 
obj-$(CONFIG_FB_VGA16)            += vga16fb.o fbcon-vga-planes.o
obj-$(CONFIG_FB_VIRGE)            += virgefb.o
obj-$(CONFIG_FB_G364)             += g364fb.o
obj-$(CONFIG_FB_FM2)              += fm2fb.o
obj-$(CONFIG_FB_CREATOR)          += creatorfb.o sbusfb.o
obj-$(CONFIG_FB_CGSIX)            += cgsixfb.o sbusfb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o sbusfb.o
obj-$(CONFIG_FB_CGTHREE)          += cgthreefb.o sbusfb.o
obj-$(CONFIG_FB_TCX)              += tcxfb.o sbusfb.o
obj-$(CONFIG_FB_CGFOURTEEN)       += cgfourteenfb.o sbusfb.o
obj-$(CONFIG_FB_P9100)            += p9100fb.o sbusfb.o
obj-$(CONFIG_FB_LEO)              += leofb.o sbusfb.o

subdir-$(CONFIG_FB_MATROX)	  += matrox
ifeq ($(CONFIG_FB_MATROX),y)
obj-y				  += matrox/matrox.o
endif

subdir-$(CONFIG_FB_RIVA)	  += riva
ifeq ($(CONFIG_FB_RIVA),y)
obj-y				  += riva/rivafb.o
endif

subdir-$(CONFIG_FB_SIS)		  += sis
ifeq ($(CONFIG_FB_SIS),y)
obj-y				  += sis/sisfb.o
endif

obj-$(CONFIG_FB_SUN3)             += sun3fb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o
obj-$(CONFIG_FB_HGA)              += hgafb.o  
obj-$(CONFIG_FB_SA1100)           += sa1100fb.o
obj-$(CONFIG_FB_VIRTUAL)          += vfb.o  
obj-$(CONFIG_FB_HIT)              += hitfb.o fbgen.o

# Generic Low Level Drivers

obj-$(CONFIG_FBCON_AFB)           += fbcon-afb.o
obj-$(CONFIG_FBCON_CFB2)          += fbcon-cfb2.o
obj-$(CONFIG_FBCON_CFB4)          += fbcon-cfb4.o
obj-$(CONFIG_FBCON_CFB8)          += fbcon-cfb8.o
obj-$(CONFIG_FBCON_CFB16)         += fbcon-cfb16.o
obj-$(CONFIG_FBCON_CFB24)         += fbcon-cfb24.o
obj-$(CONFIG_FBCON_CFB32)         += fbcon-cfb32.o
obj-$(CONFIG_FBCON_ILBM)          += fbcon-ilbm.o
obj-$(CONFIG_FBCON_IPLAN2P2)      += fbcon-iplan2p2.o
obj-$(CONFIG_FBCON_IPLAN2P4)      += fbcon-iplan2p4.o
obj-$(CONFIG_FBCON_IPLAN2P8)      += fbcon-iplan2p8.o
obj-$(CONFIG_FBCON_IPLAN2P16)     += fbcon-iplan2p16.o
obj-$(CONFIG_FBCON_MAC)           += fbcon-mac.o
obj-$(CONFIG_FBCON_MFB)           += fbcon-mfb.o
obj-$(CONFIG_FBCON_VGA)           += fbcon-vga.o
obj-$(CONFIG_FBCON_HGA)           += fbcon-hga.o

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

../conmakehash: ../conmakehash.c
	$(HOSTCC) $(HOSTCFLAGS) -o ../conmakehash ../conmakehash.c

promcon_tbl.c: prom.uni ../char/conmakehash
	../char/conmakehash prom.uni | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > promcon_tbl.c

promcon_tbl.o: promcon_tbl.c $(TOPDIR)/include/linux/types.h
