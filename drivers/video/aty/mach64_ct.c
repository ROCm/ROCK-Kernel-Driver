
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
static int aty_dsp_gt(const struct fb_info *info, u8 bpp,
		      struct pll_ct *pll);
static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per,
			     u8 bpp, union aty_pll *pll);
static u32 aty_pll_ct_to_var(const struct fb_info *info,
			     const union aty_pll *pll);



static void aty_st_pll(int offset, u8 val, const struct atyfb_par *par)
{
	/* write addr byte */
	aty_st_8(CLOCK_CNTL + 1, (offset << 2) | PLL_WR_EN, par);
	/* write the register value */
	aty_st_8(CLOCK_CNTL + 2, val, par);
	aty_st_8(CLOCK_CNTL + 1, (offset << 2) & ~PLL_WR_EN, par);
}


/* ------------------------------------------------------------------------- */

    /*
     *  PLL programming (Mach64 CT family)
     */

static int aty_dsp_gt(const struct fb_info *info, u8 bpp,
		      struct pll_ct *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u32 dsp_xclks_per_row, dsp_loop_latency, dsp_precision, dsp_off,
	    dsp_on;
	u32 xclks_per_row, fifo_off, fifo_on, y, fifo_size, page_size;

	/* xclocks_per_row<<11 */
	xclks_per_row =
	    (pll->mclk_fb_div * pll->vclk_post_div_real * 64 << 11) /
	    (pll->vclk_fb_div * pll->mclk_post_div_real * bpp);
	if (xclks_per_row < (1 << 11))
		FAIL("Dotclock to high");
	if (M64_HAS(FIFO_24)) {
		fifo_size = 24;
		dsp_loop_latency = 0;
	} else {
		fifo_size = 32;
		dsp_loop_latency = 2;
	}
	dsp_precision = 0;
	y = (xclks_per_row * fifo_size) >> 11;
	while (y) {
		y >>= 1;
		dsp_precision++;
	}
	dsp_precision -= 5;
	/* fifo_off<<6 */
	fifo_off = ((xclks_per_row * (fifo_size - 1)) >> 5) + (3 << 6);

	if (info->fix.smem_len > 1 * 1024 * 1024) {
		if (par->ram_type >= SDRAM) {
			/* >1 MB SDRAM */
			dsp_loop_latency += 8;
			page_size = 8;
		} else {
			/* >1 MB DRAM */
			dsp_loop_latency += 6;
			page_size = 9;
		}
	} else {
		if (par->ram_type >= SDRAM) {
			/* <2 MB SDRAM */
			dsp_loop_latency += 9;
			page_size = 10;
		} else {
			/* <2 MB DRAM */
			dsp_loop_latency += 8;
			page_size = 10;
		}
	}
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
	    ((dsp_loop_latency & 0xf) << 16) | ((dsp_precision & 7) << 20);
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

	/* FIXME: use the VTB/GTB /3 post divider if it's better suited */
	q = par->ref_clk_per * pll->pll_ref_div * 4 / par->mclk_per;	/* actually 8*q */
	if (q < 16 * 8 || q > 255 * 8)
		FAIL("mclk out of range");
	else if (q < 32 * 8)
		pll->mclk_post_div_real = 8;
	else if (q < 64 * 8)
		pll->mclk_post_div_real = 4;
	else if (q < 128 * 8)
		pll->mclk_post_div_real = 2;
	else
		pll->mclk_post_div_real = 1;
	pll->mclk_fb_div = q * pll->mclk_post_div_real / 8;

	/* FIXME: use the VTB/GTB /{3,6,12} post dividers if they're better suited */
	q = par->ref_clk_per * pll->pll_ref_div * 4 / vclk_per;	/* actually 8*q */
	if (q < 16 * 8 || q > 255 * 8)
		FAIL("vclk out of range");
	else if (q < 32 * 8)
		pll->vclk_post_div_real = 8;
	else if (q < 64 * 8)
		pll->vclk_post_div_real = 4;
	else if (q < 128 * 8)
		pll->vclk_post_div_real = 2;
	else
		pll->vclk_post_div_real = 1;
	pll->vclk_fb_div = q * pll->vclk_post_div_real / 8;
	return 0;
}

void aty_calc_pll_ct(const struct fb_info *info, struct pll_ct *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u8 mpostdiv = 0;
	u8 vpostdiv = 0;

	if (M64_HAS(SDRAM_MAGIC_PLL) && (par->ram_type >= SDRAM))
		pll->pll_gen_cntl = 0x04;
	else
		pll->pll_gen_cntl = 0x84;

	switch (pll->mclk_post_div_real) {
	case 1:
		mpostdiv = 0;
		break;
	case 2:
		mpostdiv = 1;
		break;
	case 3:
		mpostdiv = 4;
		break;
	case 4:
		mpostdiv = 2;
		break;
	case 8:
		mpostdiv = 3;
		break;
	}
	pll->pll_gen_cntl |= mpostdiv << 4;	/* mclk */

	if (M64_HAS(MAGIC_POSTDIV))
		pll->pll_ext_cntl = 0;
	else
		pll->pll_ext_cntl = mpostdiv;	/* xclk == mclk */

	switch (pll->vclk_post_div_real) {
	case 2:
		vpostdiv = 1;
		break;
	case 3:
		pll->pll_ext_cntl |= 0x10;
	case 1:
		vpostdiv = 0;
		break;
	case 6:
		pll->pll_ext_cntl |= 0x10;
	case 4:
		vpostdiv = 2;
		break;
	case 12:
		pll->pll_ext_cntl |= 0x10;
	case 8:
		vpostdiv = 3;
		break;
	}

	pll->pll_vclk_cntl = 0x03;	/* VCLK = PLL_VCLK/VCLKx_POST */
	pll->vclk_post_div = vpostdiv;
}

static int aty_var_to_pll_ct(const struct fb_info *info, u32 vclk_per,
			     u8 bpp, union aty_pll *pll)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	int err;

	if ((err = aty_valid_pll_ct(info, vclk_per, &pll->ct)))
		return err;
	if (M64_HAS(GTB_DSP) && (err = aty_dsp_gt(info, bpp, &pll->ct)))
		return err;
	aty_calc_pll_ct(info, &pll->ct);
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
	struct atyfb_par *par = (struct atyfb_par *) info->par;

	aty_st_pll(PLL_REF_DIV, pll->ct.pll_ref_div, par);
	aty_st_pll(PLL_GEN_CNTL, pll->ct.pll_gen_cntl, par);
	aty_st_pll(MCLK_FB_DIV, pll->ct.mclk_fb_div, par);
	aty_st_pll(PLL_VCLK_CNTL, pll->ct.pll_vclk_cntl, par);
	aty_st_pll(VCLK_POST_DIV, pll->ct.vclk_post_div, par);
	aty_st_pll(VCLK0_FB_DIV, pll->ct.vclk_fb_div, par);
	aty_st_pll(PLL_EXT_CNTL, pll->ct.pll_ext_cntl, par);

	if (M64_HAS(GTB_DSP)) {
		if (M64_HAS(XL_DLL))
			aty_st_pll(DLL_CNTL, 0x80, par);
		else if (par->ram_type >= SDRAM)
			aty_st_pll(DLL_CNTL, 0xa6, par);
		else
			aty_st_pll(DLL_CNTL, 0xa0, par);
		aty_st_pll(VFC_CNTL, 0x1b, par);
		aty_st_le32(DSP_CONFIG, pll->ct.dsp_config, par);
		aty_st_le32(DSP_ON_OFF, pll->ct.dsp_on_off, par);
	}
}

static int dummy(void)
{
	return 0;
}

const struct aty_dac_ops aty_dac_ct = {
	.set_dac	= (void *) dummy,
};

const struct aty_pll_ops aty_pll_ct = {
	.var_to_pll	= aty_var_to_pll_ct,
	.pll_to_var	= aty_pll_ct_to_var,
	.set_pll	= aty_set_pll_ct,
};
