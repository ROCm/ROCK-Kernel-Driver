/*
 * linux/drivers/video/fbgen.c -- Generic routines for frame buffer devices
 *
 *  Created 3 Jan 1998 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <video/fbcon.h>

static int currcon = 0;


/* ---- `Generic' versions of the frame buffer device operations ----------- */


    /*
     *  Get the Fixed Part of the Display
     */

int fbgen_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    char par[info2->parsize];

    if (con == -1)
	fbhw->get_par(&par, info2);
    else {
	int err;

	if ((err = fbhw->decode_var(&fb_display[con].var, &par, info2)))
	    return err;
    }
    memset(fix, 0, sizeof(struct fb_fix_screeninfo));
    return fbhw->encode_fix(fix, &par, info2);
}


    /*
     *  Get the User Defined Part of the Display
     */

int fbgen_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    char par[info2->parsize];

    if (con == -1) {
	fbhw->get_par(&par, info2);
	fbhw->encode_var(var, &par, info2);
    } else
	*var = fb_display[con].var;
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

int fbgen_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    int err;
    int oldxres, oldyres, oldbpp, oldxres_virtual, oldyres_virtual, oldyoffset;

    if ((err = fbgen_do_set_var(var, con == currcon, info2)))
	return err;
    if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldxres = fb_display[con].var.xres;
	oldyres = fb_display[con].var.yres;
	oldxres_virtual = fb_display[con].var.xres_virtual;
	oldyres_virtual = fb_display[con].var.yres_virtual;
	oldbpp = fb_display[con].var.bits_per_pixel;
	oldyoffset = fb_display[con].var.yoffset;
	fb_display[con].var = *var;
	if (oldxres != var->xres || oldyres != var->yres ||
	    oldxres_virtual != var->xres_virtual ||
	    oldyres_virtual != var->yres_virtual ||
	    oldbpp != var->bits_per_pixel ||
	    oldyoffset != var->yoffset) {
	    fbgen_set_disp(con, info2);
	    if (info->changevar)
		(*info->changevar)(con);
	    if ((err = fb_alloc_cmap(&fb_display[con].cmap, 0, 0)))
		return err;
	    fbgen_install_cmap(con, info2);
	}
    }
    var->activate = 0;
    return 0;
}


    /*
     *  Get the Colormap
     */

int fbgen_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;

    if (con == currcon)			/* current console ? */
	return fb_get_cmap(cmap, kspc, fbhw->getcolreg, info);
    else
	if (fb_display[con].cmap.len)	/* non default colormap ? */
	    fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else {
	    int size = fb_display[con].var.bits_per_pixel == 16 ? 64 : 256;
	    fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}
    return 0;
}


    /*
     *  Set the Colormap
     */

int fbgen_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    int err;

    if (!fb_display[con].cmap.len) {	/* no colormap allocated ? */
	int size = fb_display[con].var.bits_per_pixel == 16 ? 64 : 256;
	if ((err = fb_alloc_cmap(&fb_display[con].cmap, size, 0)))
	    return err;
    }
    if (con == currcon)			/* current console ? */
	return fb_set_cmap(cmap, kspc, fbhw->setcolreg, info);
    else
	fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

int fbgen_pan_display(struct fb_var_screeninfo *var, int con,
		      struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    int xoffset = var->xoffset;
    int yoffset = var->yoffset;
    int err;

    if (xoffset < 0 ||
	xoffset+fb_display[con].var.xres > fb_display[con].var.xres_virtual ||
	yoffset < 0 ||
	yoffset+fb_display[con].var.yres > fb_display[con].var.yres_virtual)
	return -EINVAL;
    if (con == currcon) {
	if (fbhw->pan_display) {
	    if ((err = fbhw->pan_display(var, info2)))
		return err;
	} else
	    return -EINVAL;
    }
    fb_display[con].var.xoffset = var->xoffset;
    fb_display[con].var.yoffset = var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
	fb_display[con].var.vmode |= FB_VMODE_YWRAP;
    else
	fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;

    return 0;
}


/* ---- Helper functions --------------------------------------------------- */


    /*
     *  Change the video mode
     */

int fbgen_do_set_var(struct fb_var_screeninfo *var, int isactive,
		     struct fb_info_gen *info)
{
    struct fbgen_hwswitch *fbhw = info->fbhw;
    int err, activate;
    char par[info->parsize];

    if ((err = fbhw->decode_var(var, &par, info)))
	return err;
    activate = var->activate;
    if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive)
	fbhw->set_par(&par, info);
    fbhw->encode_var(var, &par, info);
    var->activate = activate;
    return 0;
}


void fbgen_set_disp(int con, struct fb_info_gen *info)
{
    struct fbgen_hwswitch *fbhw = info->fbhw;
    struct fb_fix_screeninfo fix;
    char par[info->parsize];
    struct display *display;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = info->info.disp;	/* used during initialization */

    if (con == -1)
	fbhw->get_par(&par, info);
    else
	fbhw->decode_var(&fb_display[con].var, &par, info);
    memset(&fix, 0, sizeof(struct fb_fix_screeninfo));
    fbhw->encode_fix(&fix, &par, info);

    display->visual = fix.visual;
    display->type = fix.type;
    display->type_aux = fix.type_aux;
    display->ypanstep = fix.ypanstep;
    display->ywrapstep = fix.ywrapstep;
    display->line_length = fix.line_length;
    if (info->fbhw->blank || fix.visual == FB_VISUAL_PSEUDOCOLOR ||
	fix.visual == FB_VISUAL_DIRECTCOLOR)
	display->can_soft_blank = 1;
    else
	display->can_soft_blank = 0;
    fbhw->set_disp(&par, display, info);
#if 0 /* FIXME: generic inverse is not supported yet */
    display->inverse = (fix.visual == FB_VISUAL_MONO01 ? !inverse : inverse);
#else
    display->inverse = fix.visual == FB_VISUAL_MONO01;
#endif
}


    /*
     *  Install the current colormap
     */

void fbgen_install_cmap(int con, struct fb_info_gen *info)
{
    struct fbgen_hwswitch *fbhw = info->fbhw;
    if (con != currcon)
	return;
    if (fb_display[con].cmap.len)
	fb_set_cmap(&fb_display[con].cmap, 1, fbhw->setcolreg, &info->info);
    else {
	int size = fb_display[con].var.bits_per_pixel == 16 ? 64 : 256;
	fb_set_cmap(fb_default_cmap(size), 1, fbhw->setcolreg, &info->info);
    }
}


    /*
     *  Update the `var' structure (called by fbcon.c)
     */

int fbgen_update_var(int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    int err;

    if (fbhw->pan_display) {
        if ((err = fbhw->pan_display(&fb_display[con].var, info2)))
            return err;
    }
    return 0;
}


    /*
     *  Switch to a different virtual console
     */

int fbgen_switch(int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;

    /* Do we have to save the colormap ? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, 1, fbhw->getcolreg,
		    &info2->info);
    fbgen_do_set_var(&fb_display[con].var, 1, info2);
    currcon = con;
    /* Install new colormap */
    fbgen_install_cmap(con, info2);
    return 0;
}


    /*
     *  Blank the screen
     */

void fbgen_blank(int blank, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    u16 black[16];
    struct fb_cmap cmap;

    if (fbhw->blank && !fbhw->blank(blank, info2))
	return;
    if (blank) {
	memset(black, 0, 16*sizeof(u16));
	cmap.red = black;
	cmap.green = black;
	cmap.blue = black;
	cmap.transp = NULL;
	cmap.start = 0;
	cmap.len = 16;
	fb_set_cmap(&cmap, 1, fbhw->setcolreg, info);
    } else
	fbgen_install_cmap(currcon, info2);
}
