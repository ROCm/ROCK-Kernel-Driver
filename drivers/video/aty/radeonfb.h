#ifndef __RADEONFB_H__
#define __RADEONFB_H__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>


#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>

#include <asm/io.h>

#include <video/radeon.h>

/***************************************************************
 * Most of the definitions here are adapted right from XFree86 *
 ***************************************************************/


/*
 * Chip families. Must fit in the low 16 bits of a long word
 */
enum radeon_family {
	CHIP_FAMILY_UNKNOW,
	CHIP_FAMILY_LEGACY,
	CHIP_FAMILY_RADEON,
	CHIP_FAMILY_RV100,
	CHIP_FAMILY_RS100,    /* U1 (IGP320M) or A3 (IGP320)*/
	CHIP_FAMILY_RV200,
	CHIP_FAMILY_RS200,    /* U2 (IGP330M/340M/350M) or A4 (IGP330/340/345/350), RS250 (IGP 7000) */
	CHIP_FAMILY_R200,
	CHIP_FAMILY_RV250,
	CHIP_FAMILY_RS300,    /* Radeon 9000 IGP */
	CHIP_FAMILY_RV280,
	CHIP_FAMILY_R300,
	CHIP_FAMILY_R350,
	CHIP_FAMILY_RV350,
	CHIP_FAMILY_LAST,
};

/*
 * Chip flags
 */
enum radeon_chip_flags {
	CHIP_FAMILY_MASK	= 0x0000ffffUL,
	CHIP_FLAGS_MASK		= 0xffff0000UL,
	CHIP_IS_MOBILITY	= 0x00010000UL,
	CHIP_IS_IGP		= 0x00020000UL,
	CHIP_HAS_CRTC2		= 0x00040000UL,	
};


/*
 * Monitor types
 */
enum radeon_montype {
	MT_NONE = 0,
	MT_CRT,		/* CRT */
	MT_LCD,		/* LCD */
	MT_DFP,		/* DVI */
	MT_CTV,		/* composite TV */
	MT_STV		/* S-Video out */
};

/*
 * DDC i2c ports
 */
enum ddc_type {
	ddc_none,
	ddc_monid,
	ddc_dvi,
	ddc_vga,
	ddc_crt2,
};

/*
 * Connector types
 */
enum conn_type {
	conn_none,
	conn_proprietary,
	conn_crt,
	conn_DVI_I,
	conn_DVI_D,
};


/*
 * PLL infos
 */
struct pll_info {
	int ppll_max;
	int ppll_min;
	int sclk, mclk;
	int ref_div;
	int ref_clk;
};

/*
 * VRAM infos
 */
struct ram_info {
	int ml;
	int mb;
	int trcd;
	int trp;
	int twr;
	int cl;
	int tr2w;
	int loop_latency;
	int rloop;
};


/*
 * This structure contains the various registers manipulated by this
 * driver for setting or restoring a mode. It's mostly copied from
 * XFree's RADEONSaveRec structure. A few chip settings might still be
 * tweaked without beeing reflected or saved in these registers though
 */
struct radeon_regs {
	/* Common registers */
	u32		ovr_clr;
	u32		ovr_wid_left_right;
	u32		ovr_wid_top_bottom;
	u32		ov0_scale_cntl;
	u32		mpp_tb_config;
	u32		mpp_gp_config;
	u32		subpic_cntl;
	u32		viph_control;
	u32		i2c_cntl_1;
	u32		gen_int_cntl;
	u32		cap0_trig_cntl;
	u32		cap1_trig_cntl;
	u32		bus_cntl;
	u32		surface_cntl;
	u32		bios_5_scratch;

	/* Other registers to save for VT switches or driver load/unload */
	u32		dp_datatype;
	u32		rbbm_soft_reset;
	u32		clock_cntl_index;
	u32		amcgpio_en_reg;
	u32		amcgpio_mask;

	/* Surface/tiling registers */
	u32		surf_lower_bound[8];
	u32		surf_upper_bound[8];
	u32		surf_info[8];

	/* CRTC registers */
	u32		crtc_gen_cntl;
	u32		crtc_ext_cntl;
	u32		dac_cntl;
	u32		crtc_h_total_disp;
	u32		crtc_h_sync_strt_wid;
	u32		crtc_v_total_disp;
	u32		crtc_v_sync_strt_wid;
	u32		crtc_offset;
	u32		crtc_offset_cntl;
	u32		crtc_pitch;
	u32		disp_merge_cntl;
	u32		grph_buffer_cntl;
	u32		crtc_more_cntl;

	/* CRTC2 registers */
	u32		crtc2_gen_cntl;
	u32		dac2_cntl;
	u32		disp_output_cntl;
	u32		disp_hw_debug;
	u32		disp2_merge_cntl;
	u32		grph2_buffer_cntl;
	u32		crtc2_h_total_disp;
	u32		crtc2_h_sync_strt_wid;
	u32		crtc2_v_total_disp;
	u32		crtc2_v_sync_strt_wid;
	u32		crtc2_offset;
	u32		crtc2_offset_cntl;
	u32		crtc2_pitch;

	/* Flat panel regs */
	u32 		fp_crtc_h_total_disp;
	u32		fp_crtc_v_total_disp;
	u32		fp_gen_cntl;
	u32		fp2_gen_cntl;
	u32		fp_h_sync_strt_wid;
	u32		fp2_h_sync_strt_wid;
	u32		fp_horz_stretch;
	u32		fp_panel_cntl;
	u32		fp_v_sync_strt_wid;
	u32		fp2_v_sync_strt_wid;
	u32		fp_vert_stretch;
	u32		lvds_gen_cntl;
	u32		lvds_pll_cntl;
	u32		tmds_crc;
	u32		tmds_transmitter_cntl;

	/* Computed values for PLL */
	u32		dot_clock_freq;
	int		feedback_div;
	int		post_div;	

	/* PLL registers */
	u32		ppll_div_3;
	u32		ppll_ref_div;
	u32		vclk_ecp_cntl;

	/* Computed values for PLL2 */
	u32		dot_clock_freq_2;
	int		feedback_div_2;
	int		post_div_2;

	/* PLL2 registers */
	u32		p2pll_ref_div;
	u32		p2pll_div_0;
	u32		htotal_cntl2;

       	/* Palette */
	int		palette_valid;
};

struct panel_info {
	int xres, yres;
	int valid;
	int clock;
	int hOver_plus, hSync_width, hblank;
	int vOver_plus, vSync_width, vblank;
	int hAct_high, vAct_high, interlaced;
	int pwr_delay;
	int use_bios_dividers;
	int ref_divider;
	int post_divider;
	int fbk_divider;
};

struct radeonfb_info;

#ifdef CONFIG_FB_RADEON_I2C
struct radeon_i2c_chan {
	struct radeonfb_info		*rinfo;
	u32		 		ddc_reg;
	struct i2c_adapter		adapter;
	struct i2c_algo_bit_data	algo;
};
#endif

struct radeonfb_info {
	struct fb_info		*info;

	struct radeon_regs 	state;
	struct radeon_regs	init_state;

	char			name[DEVICE_NAME_SIZE];
	char			ram_type[12];

	unsigned long		mmio_base_phys;
	unsigned long		fb_base_phys;

	void __iomem		*mmio_base;
	void __iomem		*fb_base;

	unsigned long		fb_local_base;

	struct pci_dev		*pdev;

	void __iomem		*bios_seg;
	int			fp_bios_start;

	u32			pseudo_palette[17];
	struct { u8 red, green, blue, pad; }
				palette[256];

	int			chipset;
	u8			family;
	u8			rev;
	unsigned long		video_ram;
	unsigned long		mapped_vram;

	int			pitch, bpp, depth;

	int			has_CRTC2;
	int			is_mobility;
	int			is_IGP;
	int			R300_cg_workaround;
	int			reversed_DAC;
	int			reversed_TMDS;
	struct panel_info	panel_info;
	int			mon1_type;
	u8			*mon1_EDID;
	struct fb_videomode	*mon1_modedb;
	int			mon1_dbsize;
	int			mon2_type;
	u8		        *mon2_EDID;

	u32			dp_gui_master_cntl;

	struct pll_info		pll;

	struct ram_info		ram;

	int			mtrr_hdl;

	int			pm_reg;
	u32			save_regs[64];
	int			asleep;
	int			lock_blank;

	/* Lock on register access */
	spinlock_t		reg_lock;

	/* Timer used for delayed LVDS operations */
	struct timer_list	lvds_timer;
	u32			pending_lvds_gen_cntl;
	u32			pending_pixclks_cntl;

#ifdef CONFIG_FB_RADEON_I2C
	struct radeon_i2c_chan 	i2c[4];
#endif
};


#define PRIMARY_MONITOR(rinfo)	(rinfo->mon1_type)


/*
 * Debugging stuffs
 */
#ifdef CONFIG_FB_RADEON_DEBUG
#define DEBUG		1
#else
#define DEBUG		0
#endif

#if DEBUG
#define RTRACE		printk
#else
#define RTRACE		if(0) printk
#endif


/*
 * IO macros
 */

#define INREG8(addr)		readb((rinfo->mmio_base)+addr)
#define OUTREG8(addr,val)	writeb(val, (rinfo->mmio_base)+addr)
#define INREG(addr)		readl((rinfo->mmio_base)+addr)
#define OUTREG(addr,val)	writel(val, (rinfo->mmio_base)+addr)

static inline void R300_cg_workardound(struct radeonfb_info *rinfo)
{
	u32 save, tmp;
	save = INREG(CLOCK_CNTL_INDEX);
	tmp = save & ~(0x3f | PLL_WR_EN);
	OUTREG(CLOCK_CNTL_INDEX, tmp);
	tmp = INREG(CLOCK_CNTL_DATA);
	OUTREG(CLOCK_CNTL_INDEX, save);
}

#define __OUTPLL(addr,val)	\
	do {	\
		OUTREG8(CLOCK_CNTL_INDEX, (addr & 0x0000003f) | 0x00000080); \
		OUTREG(CLOCK_CNTL_DATA, val); \
} while(0)


static inline u32 __INPLL(struct radeonfb_info *rinfo, u32 addr)
{
	u32 data;
	OUTREG8(CLOCK_CNTL_INDEX, addr & 0x0000003f);
	data = (INREG(CLOCK_CNTL_DATA));
	if (rinfo->R300_cg_workaround)
		R300_cg_workardound(rinfo);
	return data;
}

static inline u32 _INPLL(struct radeonfb_info *rinfo, u32 addr)
{
       	unsigned long flags;
	u32 data;

	spin_lock_irqsave(&rinfo->reg_lock, flags);
	data = __INPLL(rinfo, addr);
	spin_unlock_irqrestore(&rinfo->reg_lock, flags);
	return data;
}

#define INPLL(addr)		_INPLL(rinfo, addr)

#define OUTPLL(addr,val)	\
	do {	\
		unsigned long flags;\
		spin_lock_irqsave(&rinfo->reg_lock, flags); \
		__OUTPLL(addr, val); \
		spin_unlock_irqrestore(&rinfo->reg_lock, flags); \
	} while(0)

#define OUTPLLP(addr,val,mask)  					\
	do {								\
		unsigned long flags;                                    \
		unsigned int _tmp;					\
		spin_lock_irqsave(&rinfo->reg_lock, flags); 		\
		_tmp  = __INPLL(rinfo,addr);				\
		_tmp &= (mask);						\
		_tmp |= (val);						\
		__OUTPLL(addr, _tmp);					\
		spin_unlock_irqrestore(&rinfo->reg_lock, flags); 	\
	} while (0)

#define OUTREGP(addr,val,mask)  					\
	do {								\
		unsigned long flags;                                    \
		unsigned int _tmp;					\
		spin_lock_irqsave(&rinfo->reg_lock, flags); 		\
		_tmp = INREG(addr);				       	\
		_tmp &= (mask);						\
		_tmp |= (val);						\
		OUTREG(addr, _tmp);					\
		spin_unlock_irqrestore(&rinfo->reg_lock, flags); 	\
	} while (0)

#define BIOS_IN8(v)  	(readb(rinfo->bios_seg + (v)))
#define BIOS_IN16(v) 	(readb(rinfo->bios_seg + (v)) | \
			  (readb(rinfo->bios_seg + (v) + 1) << 8))
#define BIOS_IN32(v) 	(readb(rinfo->bios_seg + (v)) | \
			  (readb(rinfo->bios_seg + (v) + 1) << 8) | \
			  (readb(rinfo->bios_seg + (v) + 2) << 16) | \
			  (readb(rinfo->bios_seg + (v) + 3) << 24))

/*
 * Inline utilities
 */
static inline int round_div(int num, int den)
{
        return (num + (den / 2)) / den;
}

static inline int var_to_depth(const struct fb_var_screeninfo *var)
{
	if (var->bits_per_pixel != 16)
		return var->bits_per_pixel;
	return (var->green.length == 5) ? 15 : 16;
}

static inline u32 radeon_get_dstbpp(u16 depth)
{
	switch (depth) {
       	case 8:
       		return DST_8BPP;
       	case 15:
       		return DST_15BPP;
       	case 16:
       		return DST_16BPP;
       	case 32:
       		return DST_32BPP;
       	default:
       		return 0;
	}
}

/*
 * 2D Engine helper routines
 */
static inline void radeon_engine_flush (struct radeonfb_info *rinfo)
{
	int i;

	/* initiate flush */
	OUTREGP(RB2D_DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL,
	        ~RB2D_DC_FLUSH_ALL);

	for (i=0; i < 2000000; i++) {
		if (!(INREG(RB2D_DSTCACHE_CTLSTAT) & RB2D_DC_BUSY))
			return;
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: Flush Timeout !\n");
}


static inline void _radeon_fifo_wait (struct radeonfb_info *rinfo, int entries)
{
	int i;

	for (i=0; i<2000000; i++) {
		if ((INREG(RBBM_STATUS) & 0x7f) >= entries)
			return;
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: FIFO Timeout !\n");
}


static inline void _radeon_engine_idle (struct radeonfb_info *rinfo)
{
	int i;

	/* ensure FIFO is empty before waiting for idle */
	_radeon_fifo_wait (rinfo, 64);

	for (i=0; i<2000000; i++) {
		if (((INREG(RBBM_STATUS) & GUI_ACTIVE)) == 0) {
			radeon_engine_flush (rinfo);
			return;
		}
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: Idle Timeout !\n");
}

#define radeon_engine_idle()		_radeon_engine_idle(rinfo)
#define radeon_fifo_wait(entries)	_radeon_fifo_wait(rinfo,entries)


/* I2C Functions */
extern void radeon_create_i2c_busses(struct radeonfb_info *rinfo);
extern void radeon_delete_i2c_busses(struct radeonfb_info *rinfo);
extern int radeon_probe_i2c_connector(struct radeonfb_info *rinfo, int conn, u8 **out_edid);

/* PM Functions */
extern void radeon_pm_disable_dynamic_mode(struct radeonfb_info *rinfo);
extern void radeon_pm_enable_dynamic_mode(struct radeonfb_info *rinfo);
extern int radeonfb_pci_suspend(struct pci_dev *pdev, u32 state);
extern int radeonfb_pci_resume(struct pci_dev *pdev);

/* Monitor probe functions */
extern void radeon_probe_screens(struct radeonfb_info *rinfo,
				 const char *monitor_layout, int ignore_edid);
extern void radeon_check_modes(struct radeonfb_info *rinfo, const char *mode_option);
extern int radeon_match_mode(struct radeonfb_info *rinfo,
			     struct fb_var_screeninfo *dest,
			     const struct fb_var_screeninfo *src);

/* Accel functions */
extern void radeonfb_fillrect(struct fb_info *info, const struct fb_fillrect *region);
extern void radeonfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);
extern void radeonfb_imageblit(struct fb_info *p, const struct fb_image *image);
extern int radeonfb_sync(struct fb_info *info);
extern void radeonfb_engine_init (struct radeonfb_info *rinfo);
extern void radeonfb_engine_reset(struct radeonfb_info *rinfo);

/* Other functions */
extern int radeonfb_blank(int blank, struct fb_info *info);
extern int radeonfb_set_par(struct fb_info *info);

#endif /* __RADEONFB_H__ */
