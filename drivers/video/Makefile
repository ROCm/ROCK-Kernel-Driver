# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@users.sf.net>
# Rewritten to use lists instead of if-statements.

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    	:= fbmem.o fbcmap.o fbmon.o modedb.o softcursor.o cfbfillrect.o \
		   cfbcopyarea.o cfbimgblt.o cyber2000fb.o vgastate.o

# Each configuration option enables a list of files.

obj-$(CONFIG_VT)		  += console/

obj-$(CONFIG_FB)                  += fbmem.o fbmon.o fbcmap.o modedb.o softcursor.o
# Only include macmodes.o if we have FB support and are PPC
ifeq ($(CONFIG_FB),y)
obj-$(CONFIG_PPC)                 += macmodes.o
endif

obj-$(CONFIG_FB_ACORN)            += acornfb.o
obj-$(CONFIG_FB_AMIGA)            += amifb.o
obj-$(CONFIG_FB_PM2)              += pm2fb.o
obj-$(CONFIG_FB_PM3)		  += pm3fb.o
obj-$(CONFIG_FB_APOLLO)           += dnfb.o cfbfillrect.o cfbimgblt.o
obj-$(CONFIG_FB_Q40)              += q40fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_ATARI)            += atafb.o
obj-$(CONFIG_FB_ATY128)           += aty128fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_RADEON)		  += radeonfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_NEOMAGIC)         += neofb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_IGA)              += igafb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CONTROL)          += controlfb.o
obj-$(CONFIG_FB_PLATINUM)         += platinumfb.o
obj-$(CONFIG_FB_VALKYRIE)         += valkyriefb.o
obj-$(CONFIG_FB_CT65550)          += chipsfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_ANAKIN)           += anakinfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CLPS711X)         += clps711xfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CYBER)            += cyberfb.o
obj-$(CONFIG_FB_CYBER2000)        += cyber2000fb.o
obj-$(CONFIG_FB_SGIVW)            += sgivwfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_3DFX)             += tdfxfb.o
obj-$(CONFIG_FB_MAC)              += macfb.o macmodes.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_HP300)            += hpfb.o cfbfillrect.o cfbimgblt.o
obj-$(CONFIG_FB_OF)               += offb.o cfbfillrect.o cfbimgblt.o cfbcopyarea.o
obj-$(CONFIG_FB_IMSTT)            += imsttfb.o
obj-$(CONFIG_FB_RETINAZ3)         += retz3fb.o
obj-$(CONFIG_FB_CLGEN)            += clgenfb.o
obj-$(CONFIG_FB_TRIDENT)	  += tridentfb.o cfbfillrect.o cfbimgblt.o cfbcopyarea.o
obj-$(CONFIG_FB_S3TRIO)           += S3triofb.o
obj-$(CONFIG_FB_TGA)              += tgafb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_VESA)             += vesafb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_VGA16)            += vga16fb.o cfbfillrect.o cfbcopyarea.o \
	                             cfbimgblt.o vgastate.o 
obj-$(CONFIG_FB_VIRGE)            += virgefb.o
obj-$(CONFIG_FB_G364)             += g364fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_FM2)              += fm2fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CREATOR)          += creatorfb.o sbusfb.o
obj-$(CONFIG_FB_CGSIX)            += cgsixfb.o sbusfb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o sbusfb.o
obj-$(CONFIG_FB_CGTHREE)          += cgthreefb.o sbusfb.o
obj-$(CONFIG_FB_TCX)              += tcxfb.o sbusfb.o
obj-$(CONFIG_FB_CGFOURTEEN)       += cgfourteenfb.o sbusfb.o
obj-$(CONFIG_FB_P9100)            += p9100fb.o sbusfb.o
obj-$(CONFIG_FB_LEO)              += leofb.o sbusfb.o
obj-$(CONFIG_FB_STI)              += stifb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_PMAG_BA)          += pmag-ba-fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_PMAGB_B)          += pmagb-b-fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_MAXINE)           += maxinefb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_TX3912)           += tx3912fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o

obj-$(CONFIG_FB_MATROX)		  += matrox/
obj-$(CONFIG_FB_RIVA)		  += riva/ cfbimgblt.o vgastate.o 
obj-$(CONFIG_FB_SIS)		  += sis/
obj-$(CONFIG_FB_ATY)		  += aty/ cfbimgblt.o cfbfillrect.o cfbimgblt.o
obj-$(CONFIG_FB_I810)             += i810/ cfbfillrect.o cfbcopyarea.o \
	                             cfbimgblt.o vgastate.o

obj-$(CONFIG_FB_SUN3)             += sun3fb.o
obj-$(CONFIG_FB_HGA)              += hgafb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_SA1100)           += sa1100fb.o
obj-$(CONFIG_FB_VIRTUAL)          += vfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_HIT)              += hitfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_E1355)            += epson1355fb.o
obj-$(CONFIG_FB_PVR2)             += pvr2fb.o
obj-$(CONFIG_FB_VOODOO1)          += sstfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o

# Files generated that shall be removed upon make clean
clean-files := promcon_tbl.c

$(obj)/promcon_tbl.c: $(src)/prom.uni
	$(objtree)/scripts/conmakehash $< | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > $@

