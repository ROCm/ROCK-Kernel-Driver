/*
 *  ATI Mach64 CT/VT/GT/LT Support
 */

#include <linux/fb.h>
#include <asm/io.h>
#include <video/mach64.h>
#include "atyfb.h"

/* FIXME: remove the FAIL definition */
#define FAIL(x) do { printk(x "\n"); return -EINVAL; } while (0)

static void aty_st_pll(int offset, u8 val, const struct atyfb_par *par);
static int aty_valid_pll_ct(const struct fb_info *info, u32 vclk_per,
			    struct pll_ct *pll);
static int aty_dsp_gt(const struct fb_info *info, u32 bpp, u32 stretch,
		      struct pll_ct *pll);
static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per,
			     u32 bpp, u32 stretch, union aty_pll *pll);
static u32 aty_pll_ct_to_var(const struct fb_info *info,
			     const union aty_pll *pll);

static u8 postdividers[] = {1,2,4,8,3};

static void aty_st_pll(int offset, u8 val, const struct atyfb_par *par)
{
	/* write addr byte */
	aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN, par);
	/* write the register value */
	aty_st_8(CLOCK_CNTL + 2, val, par);
	aty_st_8(CLOCK_CNTL + 1, (offset << 2), par);
}


/* ------------------------------------------------------------------------- */

    /*
     *  PLL programming (Mach64 CT family)
     */

static int aty_dsp_gt(const struct fb_info *info, u32 bpp,
		      u32 width, struct pll_ct *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 dsp_xclks_per_row, dsp_precision, dsp_off, dsp_on;
	u32 xclks_per_row, fifo_off, fifo_on, y, page_size;

	/* xclocks_per_row<<11 */
	xclks_per_row =
	    (pll->mclk_fb_div * pll->vclk_post_div_real * 64 << 11) /
	    (pll->vclk_fb_div * pll->xclk_post_div_real * bpp);
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if (width != 0)
		xclks_per_row = (xclks_per_row * par->lcd_width) / width;
#endif
	if (xclks_per_row < (1 << 11))
		FAIL("Dotclock to high");

	dsp_precision = 0;
	y = (xclks_per_row * par->fifo_size) >> 11;
	while (y) {
		y >>= 1;
		dsp_precision++;
	}
	dsp_precision -= 5;
	/* fifo_off<<6 */
	fifo_off = ((xclks_per_row * (par->fifo_size - 1)) >> 5) + (3 << 6);

	page_size = par->page_size;
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	if (width != 0)
		page_size = (page_size * par->lcd_width) / width;
#endif
	/* fifo_on<<6 */
	if (xclks_per_row >= (page_size << 11))
		fifo_on =
		    ((2 * page_size + 1) << 6) + (xclks_per_row >> 5);
	else
		fifo_on = (3 * page_size + 2) << 6;

	dsp_xclks_per_row = xclks_per_row >> dsp_precision;
	dsp_on = fifo_on >> dsp_precision;
	dsp_off = fifo_off >> dsp_precision;

	pll->dsp_config = (dsp_xclks_per_row & 0x3fff) |
	    ((par->dsp_loop_latency & 0xf) << 16) | ((dsp_precision & 7) << 20);
	pll->dsp_on_off = (dsp_on & 0x7ff) | ((dsp_off & 0x7ff) << 16);
	return 0;
}

static int aty_valid_pll_ct(const struct fb_info *info, u32 vclk_per,
			    struct pll_ct *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 q, x;		/* x is a workaround for sparc64-linux-gcc */
	x = x;			/* x is a workaround for sparc64-linux-gcc */

	pll->pll_ref_div = par->pll_per * 2 * 255 / par->ref_clk_per;

	/* FIXME: use the VTB/GTB /{3,6,12} post dividers if they're better suited */
	q = par->ref_clk_per * pll->pll_ref_div * 4 / vclk_per;	/* actually 8*q */
	if (q < 16*8 || q > 255*8)
		FAIL("vclk out of range");
	else {
		pll->vclk_post_div = 0;
		if (q < 128*8)
			pll->vclk_post_div++;
		if (q < 64*8)
			pll->vclk_post_div++;
		if (q < 32*8)
			pll->vclk_post_div++;
	}
	pll->vclk_post_div_real = postdividers[pll->vclk_post_div];
	pll->vclk_fb_div = q*pll->vclk_post_div_real/8;
	pll->pll_vclk_cntl = 0x03;	/* VCLK = PLL_VCLK/VCLKx_POST */

	return 0;
}

static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per,
			     u32 bpp, u32 width, union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	int err;

	if ((err = aty_valid_pll_ct(info, vclk_per, &pll->ct)))
		return err;
	if (M64_HAS(GTB_DSP) && (err = aty_dsp_gt(info, bpp, width, &pll->ct)))
		return err;
	return 0;
}

static u32 aty_pll_ct_to_var(const struct fb_info *info,
			     const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	u32 ref_clk_per = par->ref_clk_per;
	u8 pll_ref_div = pll->ct.pll_ref_div;
	u8 vclk_fb_div = pll->ct.vclk_fb_div;
	u8 vclk_post_div = pll->ct.vclk_post_div_real;

	return ref_clk_per * pll_ref_div * vclk_post_div / vclk_fb_div / 2;
}

void aty_set_pll_ct(const struct fb_info *info,
		    const union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *)info->par;

	u8 tmp, tmp2; u32 crtc_gen_cntl;
#ifdef DEBUG
	printk("aty_set_pll_ct: setting clock %i for FeedBackDivider %i, ReferenceDivider %i, PostDivider %i\n",
		par->clock, pll->ct.vclk_fb_div, par->pll_ref_div, pll->ct.vclk_post_div);
#endif
    /* Temporarily switch to accelerator mode */
    crtc_gen_cntl = aty_ld_le32(CRTC_GEN_CNTL, par);
    if (!(crtc_gen_cntl & CRTC_EXT_DISP_EN))
        aty_st_le32(CRTC_GEN_CNTL, crtc_gen_cntl | CRTC_EXT_DISP_EN, par);

    /* Reset VCLK generator */
    aty_st_pll(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl, par);

    /* Set post-divider */
    tmp2 = par->clock << 1;
    tmp = aty_ld_pll(VCLK_POST_DIV, par);
    tmp &= ~(0x03U << tmp2);
    tmp |= ((pll->ct.vclk_post_div & 0x03U) << tmp2);
    aty_st_pll(VCLK_POST_DIV, tmp, par);

    /* Set extended post-divider */
    tmp = aty_ld_pll(PLL_EXT_CNTL, par);
    tmp &= ~(0x10U << par->clock);
    tmp |= (((pll->ct.vclk_post_div >> 2) & 0x10U) << par->clock);
    aty_st_pll(PLL_EXT_CNTL, tmp, par);

    /* Set feedback divider */
    tmp = VCLK0_FB_DIV + par->clock;
    aty_st_pll(tmp, (pll->ct.vclk_fb_div & 0xFFU), par);

    /* End VCLK generator reset */
    aty_st_pll(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl & ~(0x04U), par);

    /* Reset write bit */
    /* ATIAccessMach64PLLReg(pATI, 0, FALSE); */

    /* Restore register */
    if (!(crtc_gen_cntl & CRTC_EXT_DISP_EN))
        aty_st_le32(CRTC_GEN_CNTL, crtc_gen_cntl, par);

    if (M64_HAS(GTB_DSP)) {
	aty_st_le32(DSP_CONFIG, pll->ct.dsp_config, par);
	aty_st_le32(DSP_ON_OFF, pll->ct.dsp_on_off, par);
    }
}


static void __init aty_init_pll_ct(struct fb_info *info)
{
	struct atyfb_par *par = (struct atyfb_par *)info->par;
	struct pll_ct *pll = &par->pll.ct;
	u8 pll_ref_div, pll_gen_cntl, pll_ext_cntl;
	u8 mpost_div, xpost_div;
	u8 sclk_post_div_real, sclk_fb_div, spll_cntl2;
	u32 q;

	if (M64_HAS(FIFO_24)) {
		par->fifo_size = 24;
		par->dsp_loop_latency = 0;
	} else {
		par->fifo_size = 32;
		par->dsp_loop_latency = 2;
	}

	if (info->fix.smem_len > 1*1024*1024) {
		if (par->ram_type >= SDRAM) {
			/* >1 MB SDRAM */
			par->dsp_loop_latency += 8;
			par->page_size = 8;
		} else {
			/* >1 MB DRAM */
			par->dsp_loop_latency += 6;
			par->page_size = 9;
		}
	} else {
		if (par->ram_type >= SDRAM) {
			/* <2 MB SDRAM */
			par->dsp_loop_latency += 9;
			par->page_size = 10;
		} else {
			/* <2 MB DRAM */
			par->dsp_loop_latency += 8;
			par->page_size = 10;
		}
	}

	/* Exit if the user does not want us to play with the clock
	   rates of her chip. */
	if (par->mclk_per == 0) {
		u16 mclk_fb_div;
		u8 pll_ext_cntl;

		pll->pll_ref_div = aty_ld_pll(PLL_REF_DIV, par);
		pll_ext_cntl = aty_ld_pll(PLL_EXT_CNTL, par);
		pll->xclk_post_div_real = postdividers[pll_ext_cntl & 7];
		mclk_fb_div = aty_ld_pll(MCLK_FB_DIV, par);
		if (pll_ext_cntl & 8)
			mclk_fb_div <<= 1;
		pll->mclk_fb_div = mclk_fb_div;
		return;
	}

	pll_ref_div = par->pll_per * 2 * 255 / par->ref_clk_per;
	pll->pll_ref_div = pll_ref_div;

	/* FIXME: use the VTB/GTB /3 post divider if it's better suited */
	q = par->ref_clk_per * pll_ref_div * 4 / par->xclk_per;	/* actually 8*q */
	if (q < 16*8 || q > 255*8) {
		printk(KERN_CRIT "xclk out of range\n");
		return;
	} else {
		xpost_div = 0;
		if (q < 128*8)
			xpost_div++;
		if (q < 64*8)
			xpost_div++;
		if (q < 32*8)
			xpost_div++;
	}
	pll->xclk_post_div_real = postdividers[xpost_div];
	pll->mclk_fb_div = q * pll->xclk_post_div_real / 8;

	if (M64_HAS(SDRAM_MAGIC_PLL) && (par->ram_type >= SDRAM))
		pll_gen_cntl = 0x04;
	else
		pll_gen_cntl = 0x84;

	if (M64_HAS(MAGIC_POSTDIV))
		pll_ext_cntl = 0;
	else
		pll_ext_cntl = xpost_div;

	if (par->mclk_per == par->xclk_per)
		pll_gen_cntl |= xpost_div<<4; /* mclk == xclk */
	else {
		pll_gen_cntl |= 6<<4;	/* mclk == sclk*/

		q = par->ref_clk_per * pll_ref_div * 4 / par->mclk_per;	/* actually 8*q */
		if (q < 16 * 8 || q > 255 * 8) {
			printk(KERN_CRIT "mclk out of range\n");
			return;
		} else {
			mpost_div = 0;
			if (q < 128*8)
				mpost_div++;
			if (q < 64*8)
				mpost_div++;
			if (q < 32*8)
				mpost_div++;
		}
		sclk_post_div_real = postdividers[mpost_div];
		sclk_fb_div = q * sclk_post_div_real / 8;
		spll_cntl2 = mpost_div << 4;
/*
		This disables the sclk, crashes the computer as reported:
		aty_st_pll(SPLL_CNTL2, 3, info);

		So it seems the sclk must be enabled before it is used;
		so PLL_GEN_CNTL must be programmed *after* the sclk.
*/
#ifdef DEBUG
		printk(KERN_INFO "sclk_fb_div: %x spll_cntl2:%x\n",
			sclk_fb_div, spll_cntl2);
#endif
		aty_st_pll(SPLL_CNTL2, spll_cntl2, par);
		aty_st_pll(SCLK_FB_DIV, sclk_fb_div, par);
	}
#ifdef DEBUG
	printk(KERN_INFO "pll_ref_div: %x pll_gencntl: %x mclk_fb_div: %x pll_ext_cntl: %x\n",
		pll_ref_div, pll_gen_cntl, par->mclk_fb_div, pll_ext_cntl);
#endif
	aty_st_pll(PLL_REF_DIV, pll_ref_div, par);
	aty_st_pll(PLL_GEN_CNTL, pll_gen_cntl, par);
	aty_st_pll(MCLK_FB_DIV, pll->mclk_fb_div, par);
	aty_st_pll(PLL_EXT_CNTL, pll_ext_cntl, par);
	if (M64_HAS(GTB_DSP)) {
		if (M64_HAS(XL_DLL))
			aty_st_pll(DLL_CNTL, 0x80, par);
		else if (par->ram_type >= SDRAM)
			aty_st_pll(DLL_CNTL, 0xa6, par);
		else
			aty_st_pll(DLL_CNTL, 0xa0, par);
		aty_st_pll(VFC_CNTL, 0x1b, par);
	}
}

static int dummy(void)
{
	return 0;
}

const struct aty_dac_ops aty_dac_ct = {
	.set_dac	= (void *)dummy,
};

const struct aty_pll_ops aty_pll_ct = {
	.var_to_pll	= aty_var_to_pll_ct,
	.pll_to_var	= aty_pll_ct_to_var,
	.set_pll	= aty_set_pll_ct,
	.init_pll	= aty_init_pll_ct
};
