/*
 *  ATI Frame Buffer Device Driver Core Definitions
 */

#include <linux/config.h>
    /*
     *  Elements of the hardware specific atyfb_par structure
     */

struct crtc {
	u32 vxres;
	u32 vyres;
	u32 h_tot_disp;
	u32 h_sync_strt_wid;
	u32 v_tot_disp;
	u32 v_sync_strt_wid;
	u32 off_pitch;
	u32 gen_cntl;
	u32 dp_pix_width;	/* acceleration */
	u32 dp_chain_mask;	/* acceleration */
};

struct pll_514 {
	u8 m;
	u8 n;
};

struct pll_18818 {
	u32 program_bits;
	u32 locationAddr;
	u32 period_in_ps;
	u32 post_divider;
};

struct pll_ct {
	u8 pll_ref_div;
	u8 pll_gen_cntl;
	u8 mclk_fb_div;
	u8 pll_vclk_cntl;
	u8 vclk_post_div;
	u8 vclk_fb_div;
	u8 pll_ext_cntl;
	u32 dsp_config;		/* Mach64 GTB DSP */
	u32 dsp_on_off;		/* Mach64 GTB DSP */
	u8 mclk_post_div_real;
	u8 vclk_post_div_real;
};

union aty_pll {
	struct pll_ct ct;
	struct pll_514 ibm514;
	struct pll_18818 ics2595;
};

    /*
     *  The hardware parameters for each card
     */

struct aty_cursor {
	u8 bits[8][64];
	u8 mask[8][64];
	u8 *ram;
};

struct atyfb_par {
	struct aty_cmap_regs *aty_cmap_regs;
	const struct aty_dac_ops *dac_ops;
	const struct aty_pll_ops *pll_ops;
	struct aty_cursor *cursor;
	unsigned long ati_regbase;
	unsigned long clk_wr_offset;
	struct crtc crtc;
	union aty_pll pll;
	u32 features;
	u32 ref_clk_per;
	u32 pll_per;
	u32 mclk_per;
	u8 bus_type;
	u8 ram_type;
	u8 mem_refresh_rate;
	u8 blitter_may_be_busy;
	u32 accel_flags;
#ifdef __sparc__
	struct pci_mmap_map *mmap_map;
	u8 mmaped;
	int open;
#endif
#ifdef CONFIG_PMAC_PBOOK
	struct fb_info *next;
	unsigned char *save_framebuffer;
	unsigned long save_pll[64];
#endif
};
    
    /*
     *  ATI Mach64 features
     */

#define M64_HAS(feature)	((par)->features & (M64F_##feature))

#define M64F_RESET_3D		0x00000001
#define M64F_MAGIC_FIFO		0x00000002
#define M64F_GTB_DSP		0x00000004
#define M64F_FIFO_24		0x00000008
#define M64F_SDRAM_MAGIC_PLL	0x00000010
#define M64F_MAGIC_POSTDIV	0x00000020
#define M64F_INTEGRATED		0x00000040
#define M64F_CT_BUS		0x00000080
#define M64F_VT_BUS		0x00000100
#define M64F_MOBIL_BUS		0x00000200
#define M64F_GX			0x00000400
#define M64F_CT			0x00000800
#define M64F_VT			0x00001000
#define M64F_GT			0x00002000
#define M64F_MAGIC_VRAM_SIZE	0x00004000
#define M64F_G3_PB_1_1		0x00008000
#define M64F_G3_PB_1024x768	0x00010000
#define M64F_EXTRA_BRIGHT	0x00020000
#define M64F_LT_SLEEP		0x00040000
#define M64F_XL_DLL		0x00080000


    /*
     *  Register access
     */

static inline u32 aty_ld_le32(int regindex, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	return in_le32((volatile u32 *)(par->ati_regbase + regindex));
#else
	return readl(par->ati_regbase + regindex);
#endif
}

static inline void aty_st_le32(int regindex, u32 val,
			       const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	out_le32((volatile u32 *)(par->ati_regbase + regindex), val);
#else
	writel(val, par->ati_regbase + regindex);
#endif
}

static inline u8 aty_ld_8(int regindex, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;
#ifdef CONFIG_ATARI
	return in_8(par->ati_regbase + regindex);
#else
	return readb(par->ati_regbase + regindex);
#endif
}

static inline void aty_st_8(int regindex, u8 val,
			    const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	out_8(par->ati_regbase + regindex, val);
#else
	writeb(val, par->ati_regbase + regindex);
#endif
}

static inline u8 aty_ld_pll(int offset, const struct atyfb_par *par)
{
	u8 res;

	/* write addr byte */
	aty_st_8(CLOCK_CNTL + 1, (offset << 2), par);
	/* read the register value */
	res = aty_ld_8(CLOCK_CNTL + 2, par);
	return res;
}


    /*
     *  DAC operations
     */

struct aty_dac_ops {
	int (*set_dac) (const struct fb_info * info,
			const union aty_pll * pll, u32 bpp, u32 accel);
};

extern const struct aty_dac_ops aty_dac_ibm514;	/* IBM RGB514 */
extern const struct aty_dac_ops aty_dac_ati68860b;	/* ATI 68860-B */
extern const struct aty_dac_ops aty_dac_att21c498;	/* AT&T 21C498 */
extern const struct aty_dac_ops aty_dac_unsupported;	/* unsupported */
extern const struct aty_dac_ops aty_dac_ct;	/* Integrated */


    /*
     *  Clock operations
     */

struct aty_pll_ops {
	int (*var_to_pll) (const struct fb_info * info, u32 vclk_per,
			   u8 bpp, union aty_pll * pll);
	 u32(*pll_to_var) (const struct fb_info * info,
			   const union aty_pll * pll);
	void (*set_pll) (const struct fb_info * info,
			 const union aty_pll * pll);
};

extern const struct aty_pll_ops aty_pll_ati18818_1;	/* ATI 18818 */
extern const struct aty_pll_ops aty_pll_stg1703;	/* STG 1703 */
extern const struct aty_pll_ops aty_pll_ch8398;	/* Chrontel 8398 */
extern const struct aty_pll_ops aty_pll_att20c408;	/* AT&T 20C408 */
extern const struct aty_pll_ops aty_pll_ibm514;	/* IBM RGB514 */
extern const struct aty_pll_ops aty_pll_unsupported;	/* unsupported */
extern const struct aty_pll_ops aty_pll_ct;	/* Integrated */


extern void aty_set_pll_ct(const struct fb_info *info,
			   const union aty_pll *pll);
extern void aty_calc_pll_ct(const struct fb_info *info,
			    struct pll_ct *pll);


    /*
     *  Hardware cursor support
     */

extern struct aty_cursor *aty_init_cursor(struct fb_info *info);
extern int atyfb_cursor(struct fb_info *info, struct fb_cursor *cursor);
extern void aty_set_cursor_color(struct fb_info *info);
extern void aty_set_cursor_shape(struct fb_info *info);

    /*
     *  Hardware acceleration
     */

static inline void wait_for_fifo(u16 entries, const struct atyfb_par *par)
{
	while ((aty_ld_le32(FIFO_STAT, par) & 0xffff) >
	       ((u32) (0x8000 >> entries)));
}

static inline void wait_for_idle(struct atyfb_par *par)
{
	wait_for_fifo(16, par);
	while ((aty_ld_le32(GUI_STAT, par) & 1) != 0);
	par->blitter_may_be_busy = 0;
}

extern void aty_reset_engine(const struct atyfb_par *par);
extern void aty_init_engine(struct atyfb_par *par,
			    struct fb_info *info);

