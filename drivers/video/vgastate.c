/*
 * linux/include/video/vgastate.c -- VGA state save/restore
 *
 * Copyright 2002 James Simmons
 * 
 * Copyright history from vga16fb.c:
 *	Copyright 1999 Ben Pfaff and Petr Vandrovec
 *	Based on VGA info at http://www.goodnet.com/~tinara/FreeVGA/home.htm
 *	Based on VESA framebuffer (c) 1998 Gerd Knorr
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>

#include "vga.h"

static inline unsigned char vga_rcrtcs(caddr_t regbase, unsigned short iobase, 
				       unsigned char reg)
{
	vga_w(regbase, iobase + 0x4, reg);
	return vga_r(regbase, iobase + 0x5);
}

static inline void vga_wcrtcs(caddr_t regbase, unsigned short iobase, 
			      unsigned char reg, unsigned char val)
{
	vga_w(regbase, iobase + 0x4, reg);
	vga_w(regbase, iobase + 0x5, val);
}

static void save_vga_text(struct fb_vgastate *state)
{
	int i;
	u8 misc, attr10, gr4, gr5, gr6, seq1, seq2, seq4;

	/* if in graphics mode, no need to save */
	attr10 = vga_rattr(state->vgabase, 0x10);
	if (attr10 & 1)
		return;
	
	/* save regs */
	misc = vga_r(state->vgabase, VGA_MIS_R);
	gr4 = vga_rgfx(state->vgabase, VGA_GFX_PLANE_READ);
	gr5 = vga_rgfx(state->vgabase, VGA_GFX_MODE);
	gr6 = vga_rgfx(state->vgabase, VGA_GFX_MISC);
	seq2 = vga_rseq(state->vgabase, VGA_SEQ_PLANE_WRITE);
	seq4 = vga_rseq(state->vgabase, VGA_SEQ_MEMORY_MODE);
	
	/* force graphics mode */
	vga_w(state->vgabase, VGA_MIS_W, misc | 1);

	/* blank screen */
	seq1 = vga_rseq(state->vgabase, VGA_SEQ_CLOCK_MODE);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 | 1 << 5);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	/* save font 0 */
	if (state->flags & VGA_SAVE_FONT0) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x4);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x2);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8 * 8192; i++) 
			state->vga_font0[i] = vga_r(state->fbbase, i);
	}
	/* save font 1 */
	if (state->flags & VGA_SAVE_FONT1) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x8);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x3);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8 * 8192; i++) 
			state->vga_font1[i] = vga_r(state->fbbase, i);
	}
	/* save font 2 */
	if (state->flags & VGA_SAVE_TEXT) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x1);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 2 * 8192; i++) 
			state->vga_text[i] = vga_r(state->fbbase, i);

		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x2);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x1);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 2 * 8192; i++) 
			state->vga_text[i] = vga_r(state->fbbase + 
						    2 * 8192, i);
	}

	/* restore regs */
	vga_wattr(state->vgabase, 0x10, attr10);

	vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, seq2);
	vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, seq4);

	vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, gr4);
	vga_wgfx(state->vgabase, VGA_GFX_MODE, gr5);
	vga_wgfx(state->vgabase, VGA_GFX_MISC, gr6);
	vga_w(state->vgabase, VGA_MIS_W, misc);

	/* unblank screen */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 & ~(1 << 5));
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1);
}

static void restore_vga_text(struct fb_vgastate *state)
{
	int i;
	u8 misc, gr1, gr3, gr4, gr5, gr6, gr8; 
	u8 seq1, seq2, seq4;

	/* save regs */
	misc = vga_r(state->vgabase, VGA_MIS_R);
	gr1 = vga_rgfx(state->vgabase, VGA_GFX_SR_ENABLE);
	gr3 = vga_rgfx(state->vgabase, VGA_GFX_DATA_ROTATE);
	gr4 = vga_rgfx(state->vgabase, VGA_GFX_PLANE_READ);
	gr5 = vga_rgfx(state->vgabase, VGA_GFX_MODE);
	gr6 = vga_rgfx(state->vgabase, VGA_GFX_MISC);
	gr8 = vga_rgfx(state->vgabase, VGA_GFX_BIT_MASK);
	seq2 = vga_rseq(state->vgabase, VGA_SEQ_PLANE_WRITE);
	seq4 = vga_rseq(state->vgabase, VGA_SEQ_MEMORY_MODE);
	
	/* force graphics mode */
	vga_w(state->vgabase, VGA_MIS_W, misc | 1);

	/* blank screen */
	seq1 = vga_rseq(state->vgabase, VGA_SEQ_CLOCK_MODE);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 | 1 << 5);
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	if (state->depth == 4) {
		vga_wgfx(state->vgabase, VGA_GFX_DATA_ROTATE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_BIT_MASK, 0xff);
		vga_wgfx(state->vgabase, VGA_GFX_SR_ENABLE, 0x00);
	}
	/* restore font 0 */
	if (state->flags & VGA_SAVE_FONT0) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x4);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x2);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8 * 8192; i++) 
			vga_w(state->fbbase, i, state->vga_font0[i]);
	}
	/* restore font 1 */
	if (state->flags & VGA_SAVE_FONT1) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x8);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x3);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 8 * 8192; i++) 
			vga_w(state->fbbase, i, state->vga_font1[i]);
	}
	/* restore font 2 */
	if (state->flags & VGA_SAVE_TEXT) {
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x1);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 2 * 8192; i++) 
			vga_w(state->fbbase, i, state->vga_text[i]);
		
		vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, 0x2);
		vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, 0x6);
		vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, 0x1);
		vga_wgfx(state->vgabase, VGA_GFX_MODE, 0x0);
		vga_wgfx(state->vgabase, VGA_GFX_MISC, 0x5);
		for (i = 0; i < 2 * 8192; i++) 
			vga_w(state->fbbase + 2 * 8192, i, 
			      state->vga_text[i]);
	}
	/* unblank screen */
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x1);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1 & ~(1 << 5));
	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x3);

	/* restore regs */
	vga_w(state->vgabase, VGA_MIS_W, misc);

	vga_wgfx(state->vgabase, VGA_GFX_SR_ENABLE, gr1);
	vga_wgfx(state->vgabase, VGA_GFX_DATA_ROTATE, gr3);
	vga_wgfx(state->vgabase, VGA_GFX_PLANE_READ, gr4);
	vga_wgfx(state->vgabase, VGA_GFX_MODE, gr5);
	vga_wgfx(state->vgabase, VGA_GFX_MISC, gr6);
	vga_wgfx(state->vgabase, VGA_GFX_BIT_MASK, gr8);

	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, seq1);
	vga_wseq(state->vgabase, VGA_SEQ_PLANE_WRITE, seq2);
	vga_wseq(state->vgabase, VGA_SEQ_MEMORY_MODE, seq4);
}
			      
static void save_vga_mode(struct fb_vgastate *state)
{
	unsigned short iobase;
	int i;

	state->misc = vga_r(state->vgabase, VGA_MIS_R);
	if (state->misc & 1)
		iobase = 0x3d0;
	else
		iobase = 0x3b0;

	for (i = 0; i < state->num_crtc; i++) 
		state->crtc[i] = vga_rcrtcs(state->vgabase, iobase, i);
	
	vga_r(state->vgabase, iobase + 0xa); 
	vga_w(state->vgabase, VGA_ATT_W, 0x00);
	for (i = 0; i < state->num_attr; i++) {
		vga_r(state->vgabase, iobase + 0xa);
		state->attr[i] = vga_rattr(state->vgabase, i);
	}
	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x20);

	for (i = 0; i < state->num_gfx; i++) 
		state->gfx[i] = vga_rgfx(state->vgabase, i);

	for (i = 0; i < state->num_seq; i++) 
		state->seq[i] = vga_rseq(state->vgabase, i);
}

static void restore_vga_mode(struct fb_vgastate *state)
{
	unsigned short iobase;
	int i;

	vga_w(state->vgabase, VGA_MIS_W, state->misc);

	if (state->misc & 1)
		iobase = 0x3d0;
	else
		iobase = 0x3b0;

	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x01);
	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, 
		 state->seq[VGA_SEQ_CLOCK_MODE] | 0x20);

	for (i = 2; i < state->num_seq; i++) 
		vga_wseq(state->vgabase, i, state->seq[i]);

	vga_wseq(state->vgabase, VGA_SEQ_RESET, 0x03);

	/* unprotect vga regs */
	vga_wcrtcs(state->vgabase, iobase, 17, state->crtc[17] & ~0x80);
	for (i = 0; i < state->num_crtc; i++) 
		vga_wcrtcs(state->vgabase, iobase, i, state->crtc[i]);
	
	for (i = 0; i < state->num_gfx; i++) 
		vga_wgfx(state->vgabase, i, state->gfx[i]);

	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x00);
	for (i = 0; i < state->num_attr; i++) {
		vga_r(state->vgabase, iobase + 0xa);
		vga_wattr(state->vgabase, i, state->attr[i]);
	}

	vga_wseq(state->vgabase, VGA_SEQ_CLOCK_MODE, 
		 state->seq[VGA_SEQ_CLOCK_MODE]);

	vga_r(state->vgabase, iobase + 0xa);
	vga_w(state->vgabase, VGA_ATT_W, 0x20);
}

static void save_vga_cmap(struct fb_vgastate *state)
{
	int i;

	vga_w(state->vgabase, VGA_PEL_MSK, 0xff);
	
	/* assumes DAC is readable and writable */
	vga_w(state->vgabase, VGA_PEL_IR, 0x00);
	for (i = 0; i < 768; i++)
		state->vga_cmap[i] = vga_r(state->vgabase, VGA_PEL_D);
}

static void restore_vga_cmap(struct fb_vgastate *state)
{
	int i;

	vga_w(state->vgabase, VGA_PEL_MSK, 0xff);

	vga_w(state->vgabase, VGA_PEL_IW, 0x00);
	for (i = 0; i < 768; i++)
		vga_w(state->vgabase, VGA_PEL_D, state->vga_cmap[i]);
}

static void vga_cleanup(struct fb_vgastate *state)
{
	if (state->vga_font0) 
		kfree(state->vga_font0);
	if (state->vga_font1) 
		kfree(state->vga_font1);
	if (state->vga_text)
		kfree(state->vga_text);
	if (state->fbbase)
		iounmap(state->fbbase);
	if (state->vga_cmap)
		kfree(state->vga_cmap);
	if (state->attr)
		kfree(state->attr);
	if (state->crtc)
		kfree(state->crtc);
	if (state->gfx)
		kfree(state->gfx);
	if (state->seq)
		kfree(state->seq);
}
		
int fb_save_vga(struct fb_vgastate *state)
{
	state->vga_font0 = NULL;
	state->vga_font1 = NULL;
	state->vga_text = NULL;
	state->vga_cmap = NULL;
	state->attr = NULL;
	state->crtc = NULL;
	state->gfx = NULL;
	state->seq = NULL;
		
	if (state->flags & VGA_SAVE_CMAP) {
		state->vga_cmap = kmalloc(768, GFP_KERNEL);
		if (!state->vga_cmap) {
			vga_cleanup(state);
			return 1;
		}
		save_vga_cmap(state);
	}

	if (state->flags & VGA_SAVE_MODE) {
		if (state->num_attr < 21)
			state->num_attr = 21;
		if (state->num_crtc < 25)
			state->num_crtc = 25;
		if (state->num_gfx < 9)
			state->num_gfx = 9;
		if (state->num_seq < 5)
			state->num_seq = 5;
		state->attr = kmalloc(state->num_attr, GFP_KERNEL);
		state->crtc = kmalloc(state->num_crtc, GFP_KERNEL);
		state->gfx = kmalloc(state->num_gfx, GFP_KERNEL);
		state->seq = kmalloc(state->num_seq, GFP_KERNEL);
		if (!state->attr || !state->crtc || !state->gfx ||
		    !state->seq) {
			vga_cleanup(state);
			return 1;
		}
		save_vga_mode(state);
	}

	if (state->flags & VGA_SAVE_FONT0) {
		state->vga_font0 = kmalloc(8192 * 8, GFP_KERNEL);
		if (!state->vga_font0) {
			vga_cleanup(state);
			return 1;
		}
	}
	if (state->flags & VGA_SAVE_FONT1) {
		state->vga_font1 = kmalloc(8192 * 8, GFP_KERNEL);
		if (!state->vga_font1) {
			vga_cleanup(state);
			return 1;
		}
	}
	if (state->flags & VGA_SAVE_TEXT) {
		state->vga_text = kmalloc(8192 * 4, GFP_KERNEL);
		if (!state->vga_text) {
			vga_cleanup(state);
			return 1;
		}
	}
	if (state->flags & VGA_SAVE_FONTS) {
		state->fbbase = ioremap(0xA0000, 8 * 8192);
		if (!state->fbbase) {
			vga_cleanup(state);
			return 1;
		}
		save_vga_text(state);
		iounmap(state->fbbase);
		state->fbbase = NULL;
	}
	return 0;
}

int fb_restore_vga (struct fb_vgastate *state)
{
	if (state->flags & VGA_SAVE_MODE)
		restore_vga_mode(state);

	if (state->flags & VGA_SAVE_FONTS) {
		state->fbbase = ioremap(0xA0000, 8 * 8192);
		if (!state->fbbase) {
			vga_cleanup(state);
			return 1;
		}
		restore_vga_text(state);
	}

	if (state->flags & VGA_SAVE_CMAP)
		restore_vga_cmap(state);

	vga_cleanup(state);
	return 0;
}

#ifdef MODULE
int init_module(void) { return 0; };
void cleanup_module(void) {};
#endif

EXPORT_SYMBOL(fb_save_vga);
EXPORT_SYMBOL(fb_restore_vga);

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("VGA State Save/Restore");
MODULE_LICENSE("GPL");

