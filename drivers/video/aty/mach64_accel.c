
/*
 *  ATI Mach64 Hardware Acceleration
 */

#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/fb.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>

#include <video/mach64.h>
#include "atyfb.h"

    /*
     *  Text console acceleration
     */

static void fbcon_aty_bmove(struct display *p, int sy, int sx, int dy,
			    int dx, int height, int width);
static void fbcon_aty_clear(struct vc_data *conp, struct display *p,
			    int sy, int sx, int height, int width);


    /*
     *  Generic Mach64 routines
     */

void aty_reset_engine(const struct atyfb_par *par)
{
	/* reset engine */
	aty_st_le32(GEN_TEST_CNTL,
		    aty_ld_le32(GEN_TEST_CNTL, par) & ~GUI_ENGINE_ENABLE,
		    par);
	/* enable engine */
	aty_st_le32(GEN_TEST_CNTL,
		    aty_ld_le32(GEN_TEST_CNTL, par) | GUI_ENGINE_ENABLE,
		    par);
	/* ensure engine is not locked up by clearing any FIFO or */
	/* HOST errors */
	aty_st_le32(BUS_CNTL,
		    aty_ld_le32(BUS_CNTL,
				par) | BUS_HOST_ERR_ACK | BUS_FIFO_ERR_ACK,
		    par);
}

static void reset_GTC_3D_engine(const struct atyfb_par *par)
{
	aty_st_le32(SCALE_3D_CNTL, 0xc0, par);
	mdelay(GTC_3D_RESET_DELAY);
	aty_st_le32(SETUP_CNTL, 0x00, par);
	mdelay(GTC_3D_RESET_DELAY);
	aty_st_le32(SCALE_3D_CNTL, 0x00, par);
	mdelay(GTC_3D_RESET_DELAY);
}

void aty_init_engine(struct atyfb_par *par, struct fb_info *info)
{
	u32 pitch_value;

	/* determine modal information from global mode structure */
	pitch_value = par->crtc.vxres;

	if (par->crtc.bpp == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		pitch_value = pitch_value * 3;
	}

	/* On GTC (RagePro), we need to reset the 3D engine before */
	if (M64_HAS(RESET_3D))
		reset_GTC_3D_engine(par);

	/* Reset engine, enable, and clear any engine errors */
	aty_reset_engine(par);
	/* Ensure that vga page pointers are set to zero - the upper */
	/* page pointers are set to 1 to handle overflows in the */
	/* lower page */
	aty_st_le32(MEM_VGA_WP_SEL, 0x00010000, par);
	aty_st_le32(MEM_VGA_RP_SEL, 0x00010000, par);

	/* ---- Setup standard engine context ---- */

	/* All GUI registers here are FIFOed - therefore, wait for */
	/* the appropriate number of empty FIFO entries */
	wait_for_fifo(14, par);

	/* enable all registers to be loaded for context loads */
	aty_st_le32(CONTEXT_MASK, 0xFFFFFFFF, par);

	/* set destination pitch to modal pitch, set offset to zero */
	aty_st_le32(DST_OFF_PITCH, (pitch_value / 8) << 22, par);

	/* zero these registers (set them to a known state) */
	aty_st_le32(DST_Y_X, 0, par);
	aty_st_le32(DST_HEIGHT, 0, par);
	aty_st_le32(DST_BRES_ERR, 0, par);
	aty_st_le32(DST_BRES_INC, 0, par);
	aty_st_le32(DST_BRES_DEC, 0, par);

	/* set destination drawing attributes */
	aty_st_le32(DST_CNTL, DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
		    DST_X_LEFT_TO_RIGHT, par);

	/* set source pitch to modal pitch, set offset to zero */
	aty_st_le32(SRC_OFF_PITCH, (pitch_value / 8) << 22, par);

	/* set these registers to a known state */
	aty_st_le32(SRC_Y_X, 0, par);
	aty_st_le32(SRC_HEIGHT1_WIDTH1, 1, par);
	aty_st_le32(SRC_Y_X_START, 0, par);
	aty_st_le32(SRC_HEIGHT2_WIDTH2, 1, par);

	/* set source pixel retrieving attributes */
	aty_st_le32(SRC_CNTL, SRC_LINE_X_LEFT_TO_RIGHT, par);

	/* set host attributes */
	wait_for_fifo(13, par);
	aty_st_le32(HOST_CNTL, 0, par);

	/* set pattern attributes */
	aty_st_le32(PAT_REG0, 0, par);
	aty_st_le32(PAT_REG1, 0, par);
	aty_st_le32(PAT_CNTL, 0, par);

	/* set scissors to modal size */
	aty_st_le32(SC_LEFT, 0, par);
	aty_st_le32(SC_TOP, 0, par);
	aty_st_le32(SC_BOTTOM, par->crtc.vyres - 1, par);
	aty_st_le32(SC_RIGHT, pitch_value - 1, par);

	/* set background color to minimum value (usually BLACK) */
	aty_st_le32(DP_BKGD_CLR, 0, par);

	/* set foreground color to maximum value (usually WHITE) */
	aty_st_le32(DP_FRGD_CLR, 0xFFFFFFFF, par);

	/* set write mask to effect all pixel bits */
	aty_st_le32(DP_WRITE_MASK, 0xFFFFFFFF, par);

	/* set foreground mix to overpaint and background mix to */
	/* no-effect */
	aty_st_le32(DP_MIX, FRGD_MIX_S | BKGD_MIX_D, par);

	/* set primary source pixel channel to foreground color */
	/* register */
	aty_st_le32(DP_SRC, FRGD_SRC_FRGD_CLR, par);

	/* set compare functionality to false (no-effect on */
	/* destination) */
	wait_for_fifo(3, par);
	aty_st_le32(CLR_CMP_CLR, 0, par);
	aty_st_le32(CLR_CMP_MASK, 0xFFFFFFFF, par);
	aty_st_le32(CLR_CMP_CNTL, 0, par);

	/* set pixel depth */
	wait_for_fifo(2, par);
	aty_st_le32(DP_PIX_WIDTH, par->crtc.dp_pix_width, par);
	aty_st_le32(DP_CHAIN_MASK, par->crtc.dp_chain_mask, par);

	wait_for_fifo(5, par);
	aty_st_le32(SCALE_3D_CNTL, 0, par);
	aty_st_le32(Z_CNTL, 0, par);
	aty_st_le32(CRTC_INT_CNTL, aty_ld_le32(CRTC_INT_CNTL, par) & ~0x20,
		    par);
	aty_st_le32(GUI_TRAJ_CNTL, 0x100023, par);

	/* insure engine is idle before leaving */
	wait_for_idle(par);
}

    /*
     *  Accelerated functions
     */

static inline void draw_rect(s16 x, s16 y, u16 width, u16 height,
			     struct atyfb_par *par)
{
	/* perform rectangle fill */
	wait_for_fifo(2, par);
	aty_st_le32(DST_Y_X, (x << 16) | y, par);
	aty_st_le32(DST_HEIGHT_WIDTH, (width << 16) | height, par);
	par->blitter_may_be_busy = 1;
}

static void atyfb_copyarea(struct fb_info *info, struct fb_copyarea *area)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	
	u32 direction = DST_LAST_PEL;
	u32 pitch_value;

	if (!area->width || !area->height)
		return;

	pitch_value = par->crtc.vxres;
	if (par->crtc.bpp == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		pitch_value *= 3;
		area->sx *= 3;
		area->dx *= 3;
		area->width *= 3;
	}

	if (area->sy < area->dy) {
		area->dy += area->height - 1;
		area->sy += area->height - 1;
	} else
		direction |= DST_Y_TOP_TO_BOTTOM;

	if (area->sx < area->dx) {
		area->dx += area->width - 1;
		area->sx += area->width - 1;
	} else
		direction |= DST_X_LEFT_TO_RIGHT;

	wait_for_fifo(4, par);
	aty_st_le32(DP_SRC, FRGD_SRC_BLIT, par);
	aty_st_le32(SRC_Y_X, (area->sx << 16) | area->sy, par);
	aty_st_le32(SRC_HEIGHT1_WIDTH1, (area->width << 16) | area->height,par);
	aty_st_le32(DST_CNTL, direction, par);
	draw_rect(area->dx, area->dy, area->width, area->height, par);
}

void atyfb_fillrect(struct fb_info *info, struct fb_fillrect *rect)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	if (!rect->width || !rect->height)
		return;

	rect->color |= (rect->color << 8);
	rect->color |= (rect->color << 16);

	if (par->crtc.bpp == 24) {
		/* In 24 bpp, the engine is in 8 bpp - this requires that all */
		/* horizontal coordinates and widths must be adjusted */
		rect->dx *= 3;
		rect->width *= 3;
	}

	wait_for_fifo(3, par);
	aty_st_le32(DP_FRGD_CLR, rect->color, par);
	aty_st_le32(DP_SRC,
		    BKGD_SRC_BKGD_CLR | FRGD_SRC_FRGD_CLR | MONO_SRC_ONE,
		    par);
	aty_st_le32(DST_CNTL,
		    DST_LAST_PEL | DST_Y_TOP_TO_BOTTOM |
		    DST_X_LEFT_TO_RIGHT, par);
	draw_rect(rect->dx, rect->dy, rect->width, rect->height, par);
}

    /*
     *  Text console acceleration
     */

static void fbcon_aty_bmove(struct display *p, int sy, int sx, int dy,
			    int dx, int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct fb_copyarea area;
#ifdef __sparc__
	struct atyfb_par *par = (struct atyfb_par *) (info->par);

	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif

	area.sx = sx * fontwidth(p);
	area.sy = sy * fontheight(p);
	area.dx = dx * fontwidth(p);
	area.dy = dy * fontheight(p);
	area.width  = width * fontwidth(p);
	area.height = height * fontheight(p);

	atyfb_copyarea(info, &area); 
}

static void fbcon_aty_clear(struct vc_data *conp, struct display *p,
			    int sy, int sx, int height, int width)
{
	struct fb_info *info = p->fb_info;
	struct fb_fillrect region;
#ifdef __sparc__
	struct atyfb_par *par = (struct atyfb_par *) (info->par);

	if (par->mmaped && (!info->display_fg
			   || info->display_fg->vc_num ==
			   par->vtconsole))
		return;
#endif
	region.color = attr_bgcol_ec(p, conp);
	region.color |= (region.color << 8);
	region.color |= (region.color << 16);

	region.dx = sx * fontwidth(p);
	region.dy = sy * fontheight(p);
	region.width = width * fontwidth(p);
	region.height = height * fontheight(p);
	region.rop = ROP_COPY;

	atyfb_fillrect(info, &region);
}

#ifdef __sparc__
#define check_access \
    if (par->mmaped && (!info->display_fg \
	|| info->display_fg->vc_num == par->vtconsole)) \
	return;
#else
#define check_access do { } while (0)
#endif

#define DEF_FBCON_ATY_OP(name, call, args...) \
static void name(struct vc_data *conp, struct display *p, args) \
{ \
    struct fb_info *info = p->fb_info; \
    struct atyfb_par *par = (struct atyfb_par *) info->par; \
    check_access; \
    if (par->blitter_may_be_busy) \
	wait_for_idle(par); \
    call; \
}

#define DEF_FBCON_ATY(width) \
    DEF_FBCON_ATY_OP(fbcon_aty##width##_putc, \
		     fbcon_cfb##width##_putc(conp, p, c, yy, xx), \
		     int c, int yy, int xx) \
    DEF_FBCON_ATY_OP(fbcon_aty##width##_putcs, \
		     fbcon_cfb##width##_putcs(conp, p, s, count, yy, xx), \
		     const unsigned short *s, int count, int yy, int xx) \
    DEF_FBCON_ATY_OP(fbcon_aty##width##_clear_margins, \
		     fbcon_cfb##width##_clear_margins(conp, p, bottom_only), \
		     int bottom_only) \
 \
struct display_switch fbcon_aty##width = { \
    setup:		fbcon_cfb##width##_setup, \
    bmove:		fbcon_aty_bmove, \
    clear:		fbcon_aty_clear, \
    putc:		fbcon_aty##width##_putc, \
    putcs:		fbcon_aty##width##_putcs, \
    revc:		fbcon_cfb##width##_revc, \
    clear_margins:	fbcon_aty##width##_clear_margins, \
    fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16) \
};

#ifdef FBCON_HAS_CFB8
DEF_FBCON_ATY(8)
#endif
#ifdef FBCON_HAS_CFB16
    DEF_FBCON_ATY(16)
#endif
#ifdef FBCON_HAS_CFB24
    DEF_FBCON_ATY(24)
#endif
#ifdef FBCON_HAS_CFB32
    DEF_FBCON_ATY(32)
#endif
