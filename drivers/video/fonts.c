/*
 * linux/drivers/video/fonts.c -- `Soft' font definitions
 *
 *    Created 1995 by Geert Uytterhoeven
 *    Rewritten 1998 by Martin Mares <mj@ucw.cz>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */


#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/setup.h>
#endif
#include <video/font.h>

#define NO_FONTS

static struct fbcon_font_desc *fbcon_fonts[] = {
#ifdef CONFIG_FONT_8x8
#undef NO_FONTS
    &font_vga_8x8,
#endif
#ifdef CONFIG_FONT_8x16
#undef NO_FONTS
    &font_vga_8x16,
#endif
#ifdef CONFIG_FONT_6x11
#if defined(CONFIG_FBCON_MAC) || defined(CONFIG_FB_SBUS)
#undef NO_FONTS
#endif
    &font_vga_6x11,
#endif
#ifdef CONFIG_FONT_SUN8x16
#undef NO_FONTS
    &font_sun_8x16,
#endif
#ifdef CONFIG_FONT_SUN12x22
#if defined(CONFIG_FB_SBUS) || defined(CONFIG_FBCON_CFB8) || defined(CONFIG_FBCON_CFB16) || defined(CONFIG_FBCON_CFB24) || defined(CONFIG_FBCON_CFB32)
#undef NO_FONTS
#endif
    &font_sun_12x22,
#endif
#ifdef CONFIG_FONT_ACORN_8x8
#undef NO_FONTS
    &font_acorn_8x8,
#endif
#ifdef CONFIG_FONT_PEARL_8x8
#undef NO_FONTS
    &font_pearl_8x8,
#endif
};

#define num_fonts (sizeof(fbcon_fonts)/sizeof(*fbcon_fonts))

#ifdef NO_FONTS
#error No fonts configured.
#endif

   /*
    *    Find a font with a specific name
    */

struct fbcon_font_desc *fbcon_find_font(char *name)
{
   unsigned int i;

   for (i = 0; i < num_fonts; i++)
      if (!strcmp(fbcon_fonts[i]->name, name))
	  return fbcon_fonts[i];
   return NULL;
}


   /*
    *    Get the default font for a specific screen size
    */

struct fbcon_font_desc *fbcon_get_default_font(int xres, int yres)
{
    int i, c, cc;
    struct fbcon_font_desc *f, *g;

    g = NULL;
    cc = -10000;
    for(i=0; i<num_fonts; i++) {
	f = fbcon_fonts[i];
	c = f->pref;
#if defined(__mc68000__) || defined(CONFIG_APUS)
#ifdef CONFIG_FONT_PEARL_8x8
	if (MACH_IS_AMIGA && f->idx == PEARL8x8_IDX)
	    c = 100;
#endif
#ifdef CONFIG_FONT_6x11
	if (MACH_IS_MAC && xres < 640 && f->idx == VGA6x11_IDX)
	    c = 100;
#endif
#endif
	if ((yres < 400) == (f->height <= 8))
	    c += 1000;
	if (c > cc) {
	    cc = c;
	    g = f;
	}
    }
    return g;
}
