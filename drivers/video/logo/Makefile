# Makefile for the Linux logos

obj-$(CONFIG_LOGO)			+= logo.o
obj-$(CONFIG_LOGO_LINUX_MONO)		+= logo_linux_mono.o
obj-$(CONFIG_LOGO_LINUX_VGA16)		+= logo_linux_vga16.o
obj-$(CONFIG_LOGO_LINUX_CLUT224)	+= logo_linux_clut224.o
obj-$(CONFIG_LOGO_DEC_CLUT224)		+= logo_dec_clut224.o
obj-$(CONFIG_LOGO_MAC_CLUT224)		+= logo_mac_clut224.o
obj-$(CONFIG_LOGO_PARISC_CLUT224)	+= logo_parisc_clut224.o
obj-$(CONFIG_LOGO_SGI_CLUT224)		+= logo_sgi_clut224.o
obj-$(CONFIG_LOGO_SUN_CLUT224)		+= logo_sun_clut224.o
obj-$(CONFIG_LOGO_SUPERH_MONO)		+= logo_superh_mono.o
obj-$(CONFIG_LOGO_SUPERH_VGA16)		+= logo_superh_vga16.o
obj-$(CONFIG_LOGO_SUPERH_CLUT224)	+= logo_superh_clut224.o

# Dependencies on generated files need to be listed explicitly

$(obj)/%_mono.o: $(src)/%_mono.c

$(obj)/%_vga16.o: $(src)/%_vga16.c

$(obj)/%_clut224.o: $(src)/%_clut224.c

$(obj)/%_gray256.o: $(src)/%_gray256.c

# How to generate them

$(obj)/%_mono.c:	$(src)/%_mono.pbm
		$(objtree)/scripts/pnmtologo -t mono -n $*_mono -o $@ $<

$(obj)/%_vga16.c:	$(src)/%_vga16.ppm
		$(objtree)/scripts/pnmtologo -t vga16 -n $*_vga16 -o $@ $<

$(obj)/%_clut224.c:	$(src)/%_clut224.ppm
		$(objtree)/scripts/pnmtologo -t clut224 -n $*_clut224 -o $@ $<

$(obj)/%_gray256.c:	$(src)/%_gray256.pgm
		$(objtree)/scripts/pnmtologo -t gray256 -n $*_gray256 -o $@ $<


# Files generated that shall be removed upon make clean
clean-files := *_mono.c *_vga16.c *_clut224.c *_gray256.c
