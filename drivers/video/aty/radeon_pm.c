#include "radeonfb.h"

#include <linux/console.h>
#include <linux/agp_backend.h>

/*
 * Currently, only PowerMac do D2 state
 */
#define CONFIG_RADEON_HAS_D2	CONFIG_PPC_PMAC

#ifdef CONFIG_RADEON_HAS_D2
/*
 * On PowerMac, we assume any mobility chip based machine does D2
 */
#ifdef CONFIG_PPC_PMAC
static inline int radeon_suspend_to_d2(struct radeonfb_info *rinfo, u32 state)
{
	return rinfo->is_mobility;
}
#else
static inline int radeon_suspend_to_d2(struct radeonfb_info *rinfo, u32 state)
{
	return 0;
}
#endif

#endif /* CONFIG_RADEON_HAS_D2 */

/*
 * Radeon M6, M7 and M9 Power Management code. This code currently
 * only supports the mobile chips in D2 mode, that is typically what
 * is used on Apple laptops, it's based from some informations provided
 * by ATI along with hours of tracing of MacOS drivers.
 * 
 * New version of this code almost totally rewritten by ATI, many thanks
 * for their support.
 */

void radeon_pm_disable_dynamic_mode(struct radeonfb_info *rinfo)
{

	u32 sclk_cntl;
	u32 mclk_cntl;
	u32 sclk_more_cntl;
	
	u32 vclk_ecp_cntl;
	u32 pixclks_cntl;

	/* Mobility chips only, untested on M9+/M10/11 */
	if (!rinfo->is_mobility)
		return;
	if (rinfo->family > CHIP_FAMILY_RV250)
		return;
	
	/* Force Core Clocks */
	sclk_cntl = INPLL( pllSCLK_CNTL_M6);
	sclk_cntl |= 	SCLK_CNTL_M6__FORCE_CP|
			SCLK_CNTL_M6__FORCE_HDP|
			SCLK_CNTL_M6__FORCE_DISP1|
			SCLK_CNTL_M6__FORCE_DISP2|
			SCLK_CNTL_M6__FORCE_TOP|
			SCLK_CNTL_M6__FORCE_E2|
			SCLK_CNTL_M6__FORCE_SE|
			SCLK_CNTL_M6__FORCE_IDCT|
			SCLK_CNTL_M6__FORCE_VIP|
			SCLK_CNTL_M6__FORCE_RE|
			SCLK_CNTL_M6__FORCE_PB|
			SCLK_CNTL_M6__FORCE_TAM|
			SCLK_CNTL_M6__FORCE_TDM|
			SCLK_CNTL_M6__FORCE_RB|
			SCLK_CNTL_M6__FORCE_TV_SCLK|
			SCLK_CNTL_M6__FORCE_SUBPIC|
			SCLK_CNTL_M6__FORCE_OV0;
    	OUTPLL( pllSCLK_CNTL_M6, sclk_cntl);
	
	
	
	sclk_more_cntl = INPLL(pllSCLK_MORE_CNTL);
	sclk_more_cntl |= 	SCLK_MORE_CNTL__FORCE_DISPREGS|
				SCLK_MORE_CNTL__FORCE_MC_GUI|
				SCLK_MORE_CNTL__FORCE_MC_HOST;	
	OUTPLL(pllSCLK_MORE_CNTL, sclk_more_cntl);
	
	/* Force Display clocks	*/
	vclk_ecp_cntl = INPLL( pllVCLK_ECP_CNTL);
	vclk_ecp_cntl &= ~(	VCLK_ECP_CNTL__PIXCLK_ALWAYS_ONb |
			 	VCLK_ECP_CNTL__PIXCLK_DAC_ALWAYS_ONb);

	OUTPLL( pllVCLK_ECP_CNTL, vclk_ecp_cntl);
	
	pixclks_cntl = INPLL( pllPIXCLKS_CNTL);
	pixclks_cntl &= ~(	PIXCLKS_CNTL__PIXCLK_GV_ALWAYS_ONb |
			 	PIXCLKS_CNTL__PIXCLK_BLEND_ALWAYS_ONb|
				PIXCLKS_CNTL__PIXCLK_DIG_TMDS_ALWAYS_ONb |
				PIXCLKS_CNTL__PIXCLK_LVDS_ALWAYS_ONb|
				PIXCLKS_CNTL__PIXCLK_TMDS_ALWAYS_ONb|
				PIXCLKS_CNTL__PIX2CLK_ALWAYS_ONb|
				PIXCLKS_CNTL__PIX2CLK_DAC_ALWAYS_ONb);
						
 	OUTPLL( pllPIXCLKS_CNTL, pixclks_cntl);

	/* Force Memory Clocks */
	mclk_cntl = INPLL( pllMCLK_CNTL_M6);
	mclk_cntl &= ~(	MCLK_CNTL_M6__FORCE_MCLKA |  
			MCLK_CNTL_M6__FORCE_MCLKB |
			MCLK_CNTL_M6__FORCE_YCLKA |
			MCLK_CNTL_M6__FORCE_YCLKB );
    	OUTPLL( pllMCLK_CNTL_M6, mclk_cntl);
}

void radeon_pm_enable_dynamic_mode(struct radeonfb_info *rinfo)
{
	u32 clk_pwrmgt_cntl;
	u32 sclk_cntl;
	u32 sclk_more_cntl;
	u32 clk_pin_cntl;
	u32 pixclks_cntl;
	u32 vclk_ecp_cntl;
	u32 mclk_cntl;
	u32 mclk_misc;

	/* Mobility chips only, untested on M9+/M10/11 */
	if (!rinfo->is_mobility)
		return;
	if (rinfo->family > CHIP_FAMILY_RV250)
		return;
	
	/* Set Latencies */
	clk_pwrmgt_cntl = INPLL( pllCLK_PWRMGT_CNTL_M6);
	
	clk_pwrmgt_cntl &= ~(	 CLK_PWRMGT_CNTL_M6__ENGINE_DYNCLK_MODE_MASK|
				 CLK_PWRMGT_CNTL_M6__ACTIVE_HILO_LAT_MASK|
				 CLK_PWRMGT_CNTL_M6__DISP_DYN_STOP_LAT_MASK|
				 CLK_PWRMGT_CNTL_M6__DYN_STOP_MODE_MASK);
	/* Mode 1 */
	clk_pwrmgt_cntl = 	CLK_PWRMGT_CNTL_M6__MC_CH_MODE|
				CLK_PWRMGT_CNTL_M6__ENGINE_DYNCLK_MODE | 
				(1<<CLK_PWRMGT_CNTL_M6__ACTIVE_HILO_LAT__SHIFT) |
				(0<<CLK_PWRMGT_CNTL_M6__DISP_DYN_STOP_LAT__SHIFT)|
				(0<<CLK_PWRMGT_CNTL_M6__DYN_STOP_MODE__SHIFT);

	OUTPLL( pllCLK_PWRMGT_CNTL_M6, clk_pwrmgt_cntl);
						

	clk_pin_cntl = INPLL( pllCLK_PIN_CNTL);
	clk_pin_cntl |= CLK_PIN_CNTL__SCLK_DYN_START_CNTL;
	 
	OUTPLL( pllCLK_PIN_CNTL, clk_pin_cntl);

	/* Enable Dyanmic mode for SCLK */

	sclk_cntl = INPLL( pllSCLK_CNTL_M6);	
	sclk_cntl &= SCLK_CNTL_M6__SCLK_SRC_SEL_MASK;
	sclk_cntl |= SCLK_CNTL_M6__FORCE_VIP;		

	OUTPLL( pllSCLK_CNTL_M6, sclk_cntl);


	sclk_more_cntl = INPLL(pllSCLK_MORE_CNTL);
	sclk_more_cntl &= ~(SCLK_MORE_CNTL__FORCE_DISPREGS);
				                    
	OUTPLL(pllSCLK_MORE_CNTL, sclk_more_cntl);

	
	/* Enable Dynamic mode for PIXCLK & PIX2CLK */

	pixclks_cntl = INPLL( pllPIXCLKS_CNTL);
	
	pixclks_cntl|=  PIXCLKS_CNTL__PIX2CLK_ALWAYS_ONb | 
			PIXCLKS_CNTL__PIX2CLK_DAC_ALWAYS_ONb|
			PIXCLKS_CNTL__PIXCLK_BLEND_ALWAYS_ONb|
			PIXCLKS_CNTL__PIXCLK_GV_ALWAYS_ONb|
			PIXCLKS_CNTL__PIXCLK_DIG_TMDS_ALWAYS_ONb|
			PIXCLKS_CNTL__PIXCLK_LVDS_ALWAYS_ONb|
			PIXCLKS_CNTL__PIXCLK_TMDS_ALWAYS_ONb;

	OUTPLL( pllPIXCLKS_CNTL, pixclks_cntl);
		
		
	vclk_ecp_cntl = INPLL( pllVCLK_ECP_CNTL);
	
	vclk_ecp_cntl|=  VCLK_ECP_CNTL__PIXCLK_ALWAYS_ONb | 
			 VCLK_ECP_CNTL__PIXCLK_DAC_ALWAYS_ONb;

	OUTPLL( pllVCLK_ECP_CNTL, vclk_ecp_cntl);


	/* Enable Dynamic mode for MCLK	*/

	mclk_cntl  = INPLL( pllMCLK_CNTL_M6);
	mclk_cntl |= 	MCLK_CNTL_M6__FORCE_MCLKA|  
			MCLK_CNTL_M6__FORCE_MCLKB|	
			MCLK_CNTL_M6__FORCE_YCLKA|
			MCLK_CNTL_M6__FORCE_YCLKB;
			
    	OUTPLL( pllMCLK_CNTL_M6, mclk_cntl);

	mclk_misc = INPLL(pllMCLK_MISC);
	mclk_misc |= 	MCLK_MISC__MC_MCLK_MAX_DYN_STOP_LAT|
			MCLK_MISC__IO_MCLK_MAX_DYN_STOP_LAT|
			MCLK_MISC__MC_MCLK_DYN_ENABLE|
			MCLK_MISC__IO_MCLK_DYN_ENABLE;	
	
	OUTPLL(pllMCLK_MISC, mclk_misc);
}

#ifdef CONFIG_PM

static void OUTMC( struct radeonfb_info *rinfo, u8 indx, u32 value)
{
	OUTREG( MC_IND_INDEX, indx | MC_IND_INDEX__MC_IND_WR_EN);	
	OUTREG( MC_IND_DATA, value);		
}

static u32 INMC(struct radeonfb_info *rinfo, u8 indx)
{
	OUTREG( MC_IND_INDEX, indx);					
	return INREG( MC_IND_DATA);
}

static void radeon_pm_save_regs(struct radeonfb_info *rinfo)
{
	rinfo->save_regs[0] = INPLL(PLL_PWRMGT_CNTL);
	rinfo->save_regs[1] = INPLL(CLK_PWRMGT_CNTL);
	rinfo->save_regs[2] = INPLL(MCLK_CNTL);
	rinfo->save_regs[3] = INPLL(SCLK_CNTL);
	rinfo->save_regs[4] = INPLL(CLK_PIN_CNTL);
	rinfo->save_regs[5] = INPLL(VCLK_ECP_CNTL);
	rinfo->save_regs[6] = INPLL(PIXCLKS_CNTL);
	rinfo->save_regs[7] = INPLL(MCLK_MISC);
	rinfo->save_regs[8] = INPLL(P2PLL_CNTL);
	
	rinfo->save_regs[9] = INREG(DISP_MISC_CNTL);
	rinfo->save_regs[10] = INREG(DISP_PWR_MAN);
	rinfo->save_regs[11] = INREG(LVDS_GEN_CNTL);
	rinfo->save_regs[12] = INREG(LVDS_PLL_CNTL);
	rinfo->save_regs[13] = INREG(TV_DAC_CNTL);
	rinfo->save_regs[14] = INREG(BUS_CNTL1);
	rinfo->save_regs[15] = INREG(CRTC_OFFSET_CNTL);
	rinfo->save_regs[16] = INREG(AGP_CNTL);
	rinfo->save_regs[17] = (INREG(CRTC_GEN_CNTL) & 0xfdffffff) | 0x04000000;
	rinfo->save_regs[18] = (INREG(CRTC2_GEN_CNTL) & 0xfdffffff) | 0x04000000;
	rinfo->save_regs[19] = INREG(GPIOPAD_A);
	rinfo->save_regs[20] = INREG(GPIOPAD_EN);
	rinfo->save_regs[21] = INREG(GPIOPAD_MASK);
	rinfo->save_regs[22] = INREG(ZV_LCDPAD_A);
	rinfo->save_regs[23] = INREG(ZV_LCDPAD_EN);
	rinfo->save_regs[24] = INREG(ZV_LCDPAD_MASK);
	rinfo->save_regs[25] = INREG(GPIO_VGA_DDC);
	rinfo->save_regs[26] = INREG(GPIO_DVI_DDC);
	rinfo->save_regs[27] = INREG(GPIO_MONID);
	rinfo->save_regs[28] = INREG(GPIO_CRT2_DDC);

	rinfo->save_regs[29] = INREG(SURFACE_CNTL);
	rinfo->save_regs[30] = INREG(MC_FB_LOCATION);
	rinfo->save_regs[31] = INREG(DISPLAY_BASE_ADDR);
	rinfo->save_regs[32] = INREG(MC_AGP_LOCATION);
	rinfo->save_regs[33] = INREG(CRTC2_DISPLAY_BASE_ADDR);
}

static void radeon_pm_restore_regs(struct radeonfb_info *rinfo)
{
	OUTPLL(P2PLL_CNTL, rinfo->save_regs[8] & 0xFFFFFFFE); /* First */
	
	OUTPLL(PLL_PWRMGT_CNTL, rinfo->save_regs[0]);
	OUTPLL(CLK_PWRMGT_CNTL, rinfo->save_regs[1]);
	OUTPLL(MCLK_CNTL, rinfo->save_regs[2]);
	OUTPLL(SCLK_CNTL, rinfo->save_regs[3]);
	OUTPLL(CLK_PIN_CNTL, rinfo->save_regs[4]);
	OUTPLL(VCLK_ECP_CNTL, rinfo->save_regs[5]);
	OUTPLL(PIXCLKS_CNTL, rinfo->save_regs[6]);
	OUTPLL(MCLK_MISC, rinfo->save_regs[7]);
	
	OUTREG(SURFACE_CNTL, rinfo->save_regs[29]);
	OUTREG(MC_FB_LOCATION, rinfo->save_regs[30]);
	OUTREG(DISPLAY_BASE_ADDR, rinfo->save_regs[31]);
	OUTREG(MC_AGP_LOCATION, rinfo->save_regs[32]);
	OUTREG(CRTC2_DISPLAY_BASE_ADDR, rinfo->save_regs[33]);

	OUTREG(DISP_MISC_CNTL, rinfo->save_regs[9]);
	OUTREG(DISP_PWR_MAN, rinfo->save_regs[10]);
	OUTREG(LVDS_GEN_CNTL, rinfo->save_regs[11]);
	OUTREG(LVDS_PLL_CNTL,rinfo->save_regs[12]);
	OUTREG(TV_DAC_CNTL, rinfo->save_regs[13]);
	OUTREG(BUS_CNTL1, rinfo->save_regs[14]);
	OUTREG(CRTC_OFFSET_CNTL, rinfo->save_regs[15]);
	OUTREG(AGP_CNTL, rinfo->save_regs[16]);
	OUTREG(CRTC_GEN_CNTL, rinfo->save_regs[17]);
	OUTREG(CRTC2_GEN_CNTL, rinfo->save_regs[18]);

	// wait VBL before that one  ?
	OUTPLL(P2PLL_CNTL, rinfo->save_regs[8]);
	
	OUTREG(GPIOPAD_A, rinfo->save_regs[19]);
	OUTREG(GPIOPAD_EN, rinfo->save_regs[20]);
	OUTREG(GPIOPAD_MASK, rinfo->save_regs[21]);
	OUTREG(ZV_LCDPAD_A, rinfo->save_regs[22]);
	OUTREG(ZV_LCDPAD_EN, rinfo->save_regs[23]);
	OUTREG(ZV_LCDPAD_MASK, rinfo->save_regs[24]);
	OUTREG(GPIO_VGA_DDC, rinfo->save_regs[25]);
	OUTREG(GPIO_DVI_DDC, rinfo->save_regs[26]);
	OUTREG(GPIO_MONID, rinfo->save_regs[27]);
	OUTREG(GPIO_CRT2_DDC, rinfo->save_regs[28]);
}

static void radeon_pm_disable_iopad(struct radeonfb_info *rinfo)
{		
	OUTREG(GPIOPAD_MASK, 0x0001ffff);
	OUTREG(GPIOPAD_EN, 0x00000400);
	OUTREG(GPIOPAD_A, 0x00000000);		
        OUTREG(ZV_LCDPAD_MASK, 0x00000000);
        OUTREG(ZV_LCDPAD_EN, 0x00000000);
      	OUTREG(ZV_LCDPAD_A, 0x00000000); 	
	OUTREG(GPIO_VGA_DDC, 0x00030000);
	OUTREG(GPIO_DVI_DDC, 0x00000000);
	OUTREG(GPIO_MONID, 0x00030000);
	OUTREG(GPIO_CRT2_DDC, 0x00000000);
}

static void radeon_pm_program_v2clk(struct radeonfb_info *rinfo)
{
	/* we use __INPLL and _OUTPLL and do the locking ourselves... */
	unsigned long flags;
	spin_lock_irqsave(&rinfo->reg_lock, flags);
	/* Set v2clk to 65MHz */
  	__OUTPLL(pllPIXCLKS_CNTL,
  		__INPLL(rinfo, pllPIXCLKS_CNTL) & ~PIXCLKS_CNTL__PIX2CLK_SRC_SEL_MASK);
	 
  	__OUTPLL(pllP2PLL_REF_DIV, 0x0000000c);
	__OUTPLL(pllP2PLL_CNTL, 0x0000bf00);
	__OUTPLL(pllP2PLL_DIV_0, 0x00020074 | P2PLL_DIV_0__P2PLL_ATOMIC_UPDATE_W);
	
	__OUTPLL(pllP2PLL_CNTL,
		__INPLL(rinfo, pllP2PLL_CNTL) & ~P2PLL_CNTL__P2PLL_SLEEP);
	mdelay(1);

	__OUTPLL(pllP2PLL_CNTL,
		__INPLL(rinfo, pllP2PLL_CNTL) & ~P2PLL_CNTL__P2PLL_RESET);
	mdelay( 1);

  	__OUTPLL(pllPIXCLKS_CNTL,
  		(__INPLL(rinfo, pllPIXCLKS_CNTL) & ~PIXCLKS_CNTL__PIX2CLK_SRC_SEL_MASK)
  		| (0x03 << PIXCLKS_CNTL__PIX2CLK_SRC_SEL__SHIFT));
	mdelay( 1);	
	spin_unlock_irqrestore(&rinfo->reg_lock, flags);
}

static void radeon_pm_low_current(struct radeonfb_info *rinfo)
{
	u32 reg;

	reg  = INREG(BUS_CNTL1);
	reg &= ~BUS_CNTL1_MOBILE_PLATFORM_SEL_MASK;
	reg |= BUS_CNTL1_AGPCLK_VALID | (1<<BUS_CNTL1_MOBILE_PLATFORM_SEL_SHIFT);
	OUTREG(BUS_CNTL1, reg);
	
	reg  = INPLL(PLL_PWRMGT_CNTL);
	reg |= PLL_PWRMGT_CNTL_SPLL_TURNOFF | PLL_PWRMGT_CNTL_PPLL_TURNOFF |
		PLL_PWRMGT_CNTL_P2PLL_TURNOFF | PLL_PWRMGT_CNTL_TVPLL_TURNOFF;
	reg &= ~PLL_PWRMGT_CNTL_SU_MCLK_USE_BCLK;
	reg &= ~PLL_PWRMGT_CNTL_MOBILE_SU;
	OUTPLL(PLL_PWRMGT_CNTL, reg);
	
	reg  = INREG(TV_DAC_CNTL);
	reg &= ~(TV_DAC_CNTL_BGADJ_MASK |TV_DAC_CNTL_DACADJ_MASK);
	reg |=TV_DAC_CNTL_BGSLEEP | TV_DAC_CNTL_RDACPD | TV_DAC_CNTL_GDACPD |
		TV_DAC_CNTL_BDACPD |
		(8<<TV_DAC_CNTL_BGADJ__SHIFT) | (8<<TV_DAC_CNTL_DACADJ__SHIFT);
	OUTREG(TV_DAC_CNTL, reg);
	
	reg  = INREG(TMDS_TRANSMITTER_CNTL);
	reg &= ~(TMDS_PLL_EN | TMDS_PLLRST);
	OUTREG(TMDS_TRANSMITTER_CNTL, reg);

	reg = INREG(DAC_CNTL);
	reg &= ~DAC_CMP_EN;
	OUTREG(DAC_CNTL, reg);

	reg = INREG(DAC_CNTL2);
	reg &= ~DAC2_CMP_EN;
	OUTREG(DAC_CNTL2, reg);
	
	reg  = INREG(TV_DAC_CNTL);
	reg &= ~TV_DAC_CNTL_DETECT;
	OUTREG(TV_DAC_CNTL, reg);
}

static void radeon_pm_setup_for_suspend(struct radeonfb_info *rinfo)
{

	u32 sclk_cntl, mclk_cntl, sclk_more_cntl;

	u32 pll_pwrmgt_cntl;
	u32 clk_pwrmgt_cntl;
	u32 clk_pin_cntl;
	u32 vclk_ecp_cntl; 
	u32 pixclks_cntl;
	u32 disp_mis_cntl;
	u32 disp_pwr_man;
	u32 tmp;
	
	/* Force Core Clocks */
	sclk_cntl = INPLL( pllSCLK_CNTL_M6);
	sclk_cntl |= 	SCLK_CNTL_M6__IDCT_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__VIP_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__RE_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__PB_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__TAM_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__TDM_MAX_DYN_STOP_LAT|
			SCLK_CNTL_M6__RB_MAX_DYN_STOP_LAT|
			
			SCLK_CNTL_M6__FORCE_DISP2|
			SCLK_CNTL_M6__FORCE_CP|
			SCLK_CNTL_M6__FORCE_HDP|
			SCLK_CNTL_M6__FORCE_DISP1|
			SCLK_CNTL_M6__FORCE_TOP|
			SCLK_CNTL_M6__FORCE_E2|
			SCLK_CNTL_M6__FORCE_SE|
			SCLK_CNTL_M6__FORCE_IDCT|
			SCLK_CNTL_M6__FORCE_VIP|
			
			SCLK_CNTL_M6__FORCE_RE|
			SCLK_CNTL_M6__FORCE_PB|
			SCLK_CNTL_M6__FORCE_TAM|
			SCLK_CNTL_M6__FORCE_TDM|
			SCLK_CNTL_M6__FORCE_RB|
			SCLK_CNTL_M6__FORCE_TV_SCLK|
			SCLK_CNTL_M6__FORCE_SUBPIC|
			SCLK_CNTL_M6__FORCE_OV0;

	OUTPLL( pllSCLK_CNTL_M6, sclk_cntl);

	sclk_more_cntl = INPLL(pllSCLK_MORE_CNTL);
	sclk_more_cntl |= 	SCLK_MORE_CNTL__FORCE_DISPREGS |
				SCLK_MORE_CNTL__FORCE_MC_GUI |
				SCLK_MORE_CNTL__FORCE_MC_HOST;

	OUTPLL(pllSCLK_MORE_CNTL, sclk_more_cntl);		

	
	mclk_cntl = INPLL( pllMCLK_CNTL_M6);
	mclk_cntl &= ~(	MCLK_CNTL_M6__FORCE_MCLKA |  
			MCLK_CNTL_M6__FORCE_MCLKB |
			MCLK_CNTL_M6__FORCE_YCLKA | 
			MCLK_CNTL_M6__FORCE_YCLKB | 
			MCLK_CNTL_M6__FORCE_MC
		      );	
    	OUTPLL( pllMCLK_CNTL_M6, mclk_cntl);
	
	/* Force Display clocks	*/
	vclk_ecp_cntl = INPLL( pllVCLK_ECP_CNTL);
	vclk_ecp_cntl &= ~(VCLK_ECP_CNTL__PIXCLK_ALWAYS_ONb |VCLK_ECP_CNTL__PIXCLK_DAC_ALWAYS_ONb);
	vclk_ecp_cntl |= VCLK_ECP_CNTL__ECP_FORCE_ON;
	OUTPLL( pllVCLK_ECP_CNTL, vclk_ecp_cntl);
	
	
	pixclks_cntl = INPLL( pllPIXCLKS_CNTL);
	pixclks_cntl &= ~(	PIXCLKS_CNTL__PIXCLK_GV_ALWAYS_ONb | 
				PIXCLKS_CNTL__PIXCLK_BLEND_ALWAYS_ONb|
				PIXCLKS_CNTL__PIXCLK_DIG_TMDS_ALWAYS_ONb |
				PIXCLKS_CNTL__PIXCLK_LVDS_ALWAYS_ONb|
				PIXCLKS_CNTL__PIXCLK_TMDS_ALWAYS_ONb|
				PIXCLKS_CNTL__PIX2CLK_ALWAYS_ONb|
				PIXCLKS_CNTL__PIX2CLK_DAC_ALWAYS_ONb);
						
 	OUTPLL( pllPIXCLKS_CNTL, pixclks_cntl);



	/* Enable System power management */
	pll_pwrmgt_cntl = INPLL( pllPLL_PWRMGT_CNTL);
	
	pll_pwrmgt_cntl |= 	PLL_PWRMGT_CNTL__SPLL_TURNOFF |
				PLL_PWRMGT_CNTL__MPLL_TURNOFF|
				PLL_PWRMGT_CNTL__PPLL_TURNOFF|
				PLL_PWRMGT_CNTL__P2PLL_TURNOFF|
				PLL_PWRMGT_CNTL__TVPLL_TURNOFF;
						
	OUTPLL( pllPLL_PWRMGT_CNTL, pll_pwrmgt_cntl);
	
	clk_pwrmgt_cntl	 = INPLL( pllCLK_PWRMGT_CNTL_M6);
	
	clk_pwrmgt_cntl &= ~(	CLK_PWRMGT_CNTL_M6__MPLL_PWRMGT_OFF|
				CLK_PWRMGT_CNTL_M6__SPLL_PWRMGT_OFF|
				CLK_PWRMGT_CNTL_M6__PPLL_PWRMGT_OFF|
				CLK_PWRMGT_CNTL_M6__P2PLL_PWRMGT_OFF|
				CLK_PWRMGT_CNTL_M6__MCLK_TURNOFF|
				CLK_PWRMGT_CNTL_M6__SCLK_TURNOFF|
				CLK_PWRMGT_CNTL_M6__PCLK_TURNOFF|
				CLK_PWRMGT_CNTL_M6__P2CLK_TURNOFF|
				CLK_PWRMGT_CNTL_M6__TVPLL_PWRMGT_OFF|
				CLK_PWRMGT_CNTL_M6__GLOBAL_PMAN_EN|
				CLK_PWRMGT_CNTL_M6__ENGINE_DYNCLK_MODE|
				CLK_PWRMGT_CNTL_M6__ACTIVE_HILO_LAT_MASK|
				CLK_PWRMGT_CNTL_M6__CG_NO1_DEBUG_MASK			
			);
						
	clk_pwrmgt_cntl |= CLK_PWRMGT_CNTL_M6__GLOBAL_PMAN_EN | CLK_PWRMGT_CNTL_M6__DISP_PM;
	
	OUTPLL( pllCLK_PWRMGT_CNTL_M6, clk_pwrmgt_cntl);	
	
	clk_pin_cntl = INPLL( pllCLK_PIN_CNTL);
	
	clk_pin_cntl &= ~CLK_PIN_CNTL__ACCESS_REGS_IN_SUSPEND;

	/* because both INPLL and OUTPLL take the same lock, that's why. */
	tmp = INPLL( pllMCLK_MISC) | MCLK_MISC__EN_MCLK_TRISTATE_IN_SUSPEND;
	OUTPLL( pllMCLK_MISC, tmp);
	
	/* AGP PLL control */
	OUTREG(BUS_CNTL1, INREG(BUS_CNTL1) |  BUS_CNTL1__AGPCLK_VALID);

	OUTREG(BUS_CNTL1,
		(INREG(BUS_CNTL1) & ~BUS_CNTL1__MOBILE_PLATFORM_SEL_MASK)
		| (2<<BUS_CNTL1__MOBILE_PLATFORM_SEL__SHIFT));	// 440BX
	OUTREG(CRTC_OFFSET_CNTL, (INREG(CRTC_OFFSET_CNTL) & ~CRTC_OFFSET_CNTL__CRTC_STEREO_SYNC_OUT_EN));
	
	clk_pin_cntl &= ~CLK_PIN_CNTL__CG_CLK_TO_OUTPIN;
	clk_pin_cntl |= CLK_PIN_CNTL__XTALIN_ALWAYS_ONb;	
	OUTPLL( pllCLK_PIN_CNTL, clk_pin_cntl);

	/* Solano2M */
	OUTREG(AGP_CNTL,
		(INREG(AGP_CNTL) & ~(AGP_CNTL__MAX_IDLE_CLK_MASK))
		| (0x20<<AGP_CNTL__MAX_IDLE_CLK__SHIFT));

	/* ACPI mode */
	/* because both INPLL and OUTPLL take the same lock, that's why. */
	tmp = INPLL( pllPLL_PWRMGT_CNTL) & ~PLL_PWRMGT_CNTL__PM_MODE_SEL;
	OUTPLL( pllPLL_PWRMGT_CNTL, tmp);


	disp_mis_cntl = INREG(DISP_MISC_CNTL);
	
	disp_mis_cntl &= ~(	DISP_MISC_CNTL__SOFT_RESET_GRPH_PP | 
				DISP_MISC_CNTL__SOFT_RESET_SUBPIC_PP | 
				DISP_MISC_CNTL__SOFT_RESET_OV0_PP |
				DISP_MISC_CNTL__SOFT_RESET_GRPH_SCLK|
				DISP_MISC_CNTL__SOFT_RESET_SUBPIC_SCLK|
				DISP_MISC_CNTL__SOFT_RESET_OV0_SCLK|
				DISP_MISC_CNTL__SOFT_RESET_GRPH2_PP|
				DISP_MISC_CNTL__SOFT_RESET_GRPH2_SCLK|
				DISP_MISC_CNTL__SOFT_RESET_LVDS|
				DISP_MISC_CNTL__SOFT_RESET_TMDS|
				DISP_MISC_CNTL__SOFT_RESET_DIG_TMDS|
				DISP_MISC_CNTL__SOFT_RESET_TV);
	
	OUTREG(DISP_MISC_CNTL, disp_mis_cntl);					
						
	disp_pwr_man = INREG(DISP_PWR_MAN);
	
	disp_pwr_man &= ~(	DISP_PWR_MAN__DISP_PWR_MAN_D3_CRTC_EN	| 
						DISP_PWR_MAN__DISP2_PWR_MAN_D3_CRTC2_EN |
						DISP_PWR_MAN__DISP_PWR_MAN_DPMS_MASK|		
						DISP_PWR_MAN__DISP_D3_RST|
						DISP_PWR_MAN__DISP_D3_REG_RST
					);
	
	disp_pwr_man |= DISP_PWR_MAN__DISP_D3_GRPH_RST|
					DISP_PWR_MAN__DISP_D3_SUBPIC_RST|
					DISP_PWR_MAN__DISP_D3_OV0_RST|
					DISP_PWR_MAN__DISP_D1D2_GRPH_RST|
					DISP_PWR_MAN__DISP_D1D2_SUBPIC_RST|
					DISP_PWR_MAN__DISP_D1D2_OV0_RST|
					DISP_PWR_MAN__DIG_TMDS_ENABLE_RST|
					DISP_PWR_MAN__TV_ENABLE_RST| 
//					DISP_PWR_MAN__AUTO_PWRUP_EN|
					0;
	
	OUTREG(DISP_PWR_MAN, disp_pwr_man);					
							
	clk_pwrmgt_cntl = INPLL( pllCLK_PWRMGT_CNTL_M6);
	pll_pwrmgt_cntl = INPLL( pllPLL_PWRMGT_CNTL) ;
	clk_pin_cntl 	= INPLL( pllCLK_PIN_CNTL);
	disp_pwr_man	= INREG(DISP_PWR_MAN);
		
	
	/* D2 */
	clk_pwrmgt_cntl |= CLK_PWRMGT_CNTL_M6__DISP_PM;
	pll_pwrmgt_cntl |= PLL_PWRMGT_CNTL__MOBILE_SU | PLL_PWRMGT_CNTL__SU_SCLK_USE_BCLK;
	clk_pin_cntl	|= CLK_PIN_CNTL__XTALIN_ALWAYS_ONb;
	disp_pwr_man 	&= ~(DISP_PWR_MAN__DISP_PWR_MAN_D3_CRTC_EN_MASK | DISP_PWR_MAN__DISP2_PWR_MAN_D3_CRTC2_EN_MASK);							
						

	OUTPLL( pllCLK_PWRMGT_CNTL_M6, clk_pwrmgt_cntl);
	OUTPLL( pllPLL_PWRMGT_CNTL, pll_pwrmgt_cntl);
	OUTPLL( pllCLK_PIN_CNTL, clk_pin_cntl);
	OUTREG(DISP_PWR_MAN, disp_pwr_man);

	/* disable display request & disable display */
	OUTREG( CRTC_GEN_CNTL, (INREG( CRTC_GEN_CNTL) & ~CRTC_GEN_CNTL__CRTC_EN) | CRTC_GEN_CNTL__CRTC_DISP_REQ_EN_B);
	OUTREG( CRTC2_GEN_CNTL, (INREG( CRTC2_GEN_CNTL) & ~CRTC2_GEN_CNTL__CRTC2_EN) | CRTC2_GEN_CNTL__CRTC2_DISP_REQ_EN_B);

	mdelay(17);				   

}

static void radeon_pm_yclk_mclk_sync(struct radeonfb_info *rinfo)
{
	u32 mc_chp_io_cntl_a1, mc_chp_io_cntl_b1;

	mc_chp_io_cntl_a1 = INMC( rinfo, ixMC_CHP_IO_CNTL_A1) & ~MC_CHP_IO_CNTL_A1__MEM_SYNC_ENA_MASK;
	mc_chp_io_cntl_b1 = INMC( rinfo, ixMC_CHP_IO_CNTL_B1) & ~MC_CHP_IO_CNTL_B1__MEM_SYNC_ENB_MASK;

	OUTMC( rinfo, ixMC_CHP_IO_CNTL_A1, mc_chp_io_cntl_a1 | (1<<MC_CHP_IO_CNTL_A1__MEM_SYNC_ENA__SHIFT));
	OUTMC( rinfo, ixMC_CHP_IO_CNTL_B1, mc_chp_io_cntl_b1 | (1<<MC_CHP_IO_CNTL_B1__MEM_SYNC_ENB__SHIFT));

	/* Wassup ? This doesn't seem to be defined, let's hope we are ok this way --BenH */
#ifdef MCLK_YCLK_SYNC_ENABLE
	mc_chp_io_cntl_a1 |= (2<<MC_CHP_IO_CNTL_A1__MEM_SYNC_ENA__SHIFT);
	mc_chp_io_cntl_b1 |= (2<<MC_CHP_IO_CNTL_B1__MEM_SYNC_ENB__SHIFT);
#endif

	OUTMC( rinfo, ixMC_CHP_IO_CNTL_A1, mc_chp_io_cntl_a1);
	OUTMC( rinfo, ixMC_CHP_IO_CNTL_B1, mc_chp_io_cntl_b1);

	mdelay( 1);
}

static void radeon_pm_program_mode_reg(struct radeonfb_info *rinfo, u16 value, u8 delay_required)
{  
	u32 mem_sdram_mode;

	mem_sdram_mode  = INREG( MEM_SDRAM_MODE_REG);

	mem_sdram_mode &= ~MEM_SDRAM_MODE_REG__MEM_MODE_REG_MASK;
	mem_sdram_mode |= (value<<MEM_SDRAM_MODE_REG__MEM_MODE_REG__SHIFT) | MEM_SDRAM_MODE_REG__MEM_CFG_TYPE;
	OUTREG( MEM_SDRAM_MODE_REG, mem_sdram_mode);

	mem_sdram_mode |=  MEM_SDRAM_MODE_REG__MEM_SDRAM_RESET;
	OUTREG( MEM_SDRAM_MODE_REG, mem_sdram_mode);

	mem_sdram_mode &= ~MEM_SDRAM_MODE_REG__MEM_SDRAM_RESET;
	OUTREG( MEM_SDRAM_MODE_REG, mem_sdram_mode);

	if (delay_required == 1)
		while( (INREG( MC_STATUS) & (MC_STATUS__MEM_PWRUP_COMPL_A | MC_STATUS__MEM_PWRUP_COMPL_B) ) == 0 )
			{ }; 	
}


static void radeon_pm_enable_dll(struct radeonfb_info *rinfo)
{  
#define DLL_RESET_DELAY 	5
#define DLL_SLEEP_DELAY		1

	u32 DLL_CKO_Value = INPLL(pllMDLL_CKO)   | MDLL_CKO__MCKOA_SLEEP |  MDLL_CKO__MCKOA_RESET;
	u32 DLL_CKA_Value = INPLL(pllMDLL_RDCKA) | MDLL_RDCKA__MRDCKA0_SLEEP | MDLL_RDCKA__MRDCKA1_SLEEP | MDLL_RDCKA__MRDCKA0_RESET | MDLL_RDCKA__MRDCKA1_RESET;
	u32 DLL_CKB_Value = INPLL(pllMDLL_RDCKB) | MDLL_RDCKB__MRDCKB0_SLEEP | MDLL_RDCKB__MRDCKB1_SLEEP | MDLL_RDCKB__MRDCKB0_RESET | MDLL_RDCKB__MRDCKB1_RESET;

	/* Setting up the DLL range for write */
	OUTPLL(pllMDLL_CKO,   	DLL_CKO_Value);
	OUTPLL(pllMDLL_RDCKA,  	DLL_CKA_Value);
	OUTPLL(pllMDLL_RDCKB,	DLL_CKB_Value);

	mdelay( DLL_RESET_DELAY);

	/* Channel A */

	/* Power Up */
	DLL_CKO_Value &= ~(MDLL_CKO__MCKOA_SLEEP );
	OUTPLL(pllMDLL_CKO,   	DLL_CKO_Value);
	mdelay( DLL_SLEEP_DELAY);  		
   
	DLL_CKO_Value &= ~(MDLL_CKO__MCKOA_RESET );
	OUTPLL(pllMDLL_CKO,	DLL_CKO_Value);
	mdelay( DLL_RESET_DELAY);  		

	/* Power Up */
	DLL_CKA_Value &= ~(MDLL_RDCKA__MRDCKA0_SLEEP );
	OUTPLL(pllMDLL_RDCKA,  	DLL_CKA_Value);
	mdelay( DLL_SLEEP_DELAY);  		

	DLL_CKA_Value &= ~(MDLL_RDCKA__MRDCKA0_RESET );
	OUTPLL(pllMDLL_RDCKA,	DLL_CKA_Value);
	mdelay( DLL_RESET_DELAY);  		

	/* Power Up */
	DLL_CKA_Value &= ~(MDLL_RDCKA__MRDCKA1_SLEEP);
	OUTPLL(pllMDLL_RDCKA,	DLL_CKA_Value);
	mdelay( DLL_SLEEP_DELAY);  		

	DLL_CKA_Value &= ~(MDLL_RDCKA__MRDCKA1_RESET);
	OUTPLL(pllMDLL_RDCKA,	DLL_CKA_Value);
	mdelay( DLL_RESET_DELAY);  		


	/* Channel B */

	/* Power Up */
	DLL_CKO_Value &= ~(MDLL_CKO__MCKOB_SLEEP );
	OUTPLL(pllMDLL_CKO,   	DLL_CKO_Value);
	mdelay( DLL_SLEEP_DELAY);  		
   
	DLL_CKO_Value &= ~(MDLL_CKO__MCKOB_RESET );
	OUTPLL(pllMDLL_CKO,   	DLL_CKO_Value);
	mdelay( DLL_RESET_DELAY);  		

	/* Power Up */
	DLL_CKB_Value &= ~(MDLL_RDCKB__MRDCKB0_SLEEP);
	OUTPLL(pllMDLL_RDCKB,   DLL_CKB_Value);
	mdelay( DLL_SLEEP_DELAY);  		

	DLL_CKB_Value &= ~(MDLL_RDCKB__MRDCKB0_RESET);
	OUTPLL(pllMDLL_RDCKB,   DLL_CKB_Value);
	mdelay( DLL_RESET_DELAY);  		

	/* Power Up */
	DLL_CKB_Value &= ~(MDLL_RDCKB__MRDCKB1_SLEEP);
	OUTPLL(pllMDLL_RDCKB,   DLL_CKB_Value);
	mdelay( DLL_SLEEP_DELAY);  		

	DLL_CKB_Value &= ~(MDLL_RDCKB__MRDCKB1_RESET);
	OUTPLL(pllMDLL_RDCKB,   DLL_CKB_Value);
	mdelay( DLL_RESET_DELAY);  		

#undef DLL_RESET_DELAY 
#undef DLL_SLEEP_DELAY
}

static void radeon_pm_full_reset_sdram(struct radeonfb_info *rinfo)
{
	u32 crtcGenCntl, crtcGenCntl2, memRefreshCntl, crtc_more_cntl, fp_gen_cntl, fp2_gen_cntl;
 
	crtcGenCntl  = INREG( CRTC_GEN_CNTL);
	crtcGenCntl2 = INREG( CRTC2_GEN_CNTL);

	memRefreshCntl 	= INREG( MEM_REFRESH_CNTL);
	crtc_more_cntl 	= INREG( CRTC_MORE_CNTL);
	fp_gen_cntl 	= INREG( FP_GEN_CNTL);
	fp2_gen_cntl 	= INREG( FP2_GEN_CNTL);
 

	OUTREG( CRTC_MORE_CNTL, 	0);
	OUTREG( FP_GEN_CNTL, 	0);
	OUTREG( FP2_GEN_CNTL, 	0);
 
	OUTREG( CRTC_GEN_CNTL,  (crtcGenCntl | CRTC_GEN_CNTL__CRTC_DISP_REQ_EN_B) );
	OUTREG( CRTC2_GEN_CNTL, (crtcGenCntl2 | CRTC2_GEN_CNTL__CRTC2_DISP_REQ_EN_B) );
  
	/* Disable refresh */
	OUTREG( MEM_REFRESH_CNTL, memRefreshCntl | MEM_REFRESH_CNTL__MEM_REFRESH_DIS);
 
	/* Reset memory */
	OUTREG( MEM_SDRAM_MODE_REG,
		INREG( MEM_SDRAM_MODE_REG) & ~MEM_SDRAM_MODE_REG__MC_INIT_COMPLETE); // Init  Not Complete

	/* DLL */
	radeon_pm_enable_dll(rinfo);

	// MLCK /YCLK sync 
	radeon_pm_yclk_mclk_sync(rinfo);

       	/* M6, M7 and M9 so far ... */
	if (rinfo->is_mobility && rinfo->family <= CHIP_FAMILY_RV250) {
		radeon_pm_program_mode_reg(rinfo, 0x2000, 1);   
		radeon_pm_program_mode_reg(rinfo, 0x2001, 1);   
		radeon_pm_program_mode_reg(rinfo, 0x2002, 1);   
		radeon_pm_program_mode_reg(rinfo, 0x0132, 1);   
		radeon_pm_program_mode_reg(rinfo, 0x0032, 1); 
	}	

	OUTREG( MEM_SDRAM_MODE_REG,
		INREG( MEM_SDRAM_MODE_REG) |  MEM_SDRAM_MODE_REG__MC_INIT_COMPLETE); // Init Complete

	OUTREG( MEM_REFRESH_CNTL, 	memRefreshCntl);

	OUTREG( CRTC_GEN_CNTL, 		crtcGenCntl);
	OUTREG( CRTC2_GEN_CNTL, 	crtcGenCntl2);
	OUTREG( FP_GEN_CNTL, 		fp_gen_cntl);
	OUTREG( FP2_GEN_CNTL, 		fp2_gen_cntl);

	OUTREG( CRTC_MORE_CNTL, 	crtc_more_cntl);

	mdelay( 15);
}

static void radeon_set_suspend(struct radeonfb_info *rinfo, int suspend)
{
	u16 pwr_cmd;
	u32 tmp;

	if (!rinfo->pm_reg)
		return;

	/* Set the chip into appropriate suspend mode (we use D2,
	 * D3 would require a compete re-initialization of the chip,
	 * including PCI config registers, clocks, AGP conf, ...)
	 */
	if (suspend) {
		printk(KERN_DEBUG "radeonfb: switching to D2 state...\n");

		/* Disable dynamic power management of clocks for the
		 * duration of the suspend/resume process
		 */
		radeon_pm_disable_dynamic_mode(rinfo);
		/* Save some registers */
		radeon_pm_save_regs(rinfo);

		/* Prepare mobility chips for suspend. Only do that on <= RV250 chips that
		 * have been tested
		 */
		if (rinfo->is_mobility && rinfo->family <= CHIP_FAMILY_RV250) {
			/* Program V2CLK */
			radeon_pm_program_v2clk(rinfo);
		
			/* Disable IO PADs */
			radeon_pm_disable_iopad(rinfo);

			/* Set low current */
			radeon_pm_low_current(rinfo);

			/* Prepare chip for power management */
			radeon_pm_setup_for_suspend(rinfo);

			/* Reset the MDLL */
			/* because both INPLL and OUTPLL take the same lock, that's why. */
			tmp = INPLL( pllMDLL_CKO) | MDLL_CKO__MCKOA_RESET | MDLL_CKO__MCKOB_RESET;
			OUTPLL( pllMDLL_CKO, tmp );
		}

		/* Switch PCI power managment to D2. */
		for (;;) {
			pci_read_config_word(
				rinfo->pdev, rinfo->pm_reg+PCI_PM_CTRL,
				&pwr_cmd);
			if (pwr_cmd & 2)
				break;			
			pci_write_config_word(
				rinfo->pdev, rinfo->pm_reg+PCI_PM_CTRL,
				(pwr_cmd & ~PCI_PM_CTRL_STATE_MASK) | 2);
			mdelay(500);
		}
	} else {
		printk(KERN_DEBUG "radeonfb: switching to D0 state...\n");

		/* Switch back PCI powermanagment to D0 */
		mdelay(200);
		pci_write_config_word(rinfo->pdev, rinfo->pm_reg+PCI_PM_CTRL, 0);
		mdelay(500);

		/* Reset the SDRAM controller  */
       		radeon_pm_full_reset_sdram(rinfo);
		
		/* Restore some registers */
		radeon_pm_restore_regs(rinfo);
		radeon_pm_enable_dynamic_mode(rinfo);
	}
}

int radeonfb_pci_suspend(struct pci_dev *pdev, u32 state)
{
        struct fb_info *info = pci_get_drvdata(pdev);
        struct radeonfb_info *rinfo = info->par;

	/* We don't do anything but D2, for now we return 0, but
	 * we may want to change that. How do we know if the BIOS
	 * can properly take care of D3 ? Also, with swsusp, we
	 * know we'll be rebooted, ...
	 */

	printk(KERN_DEBUG "radeonfb: suspending to state: %d...\n", state);
	
	acquire_console_sem();

	/* Userland should do this but doesn't... bridge gets suspended
	 * too late. Unfortunately, that works only when AGP is built-in,
	 * not for a module.
	 */
#ifdef CONFIG_AGP
	agp_enable(0);
#endif

	fb_set_suspend(info, 1);

	if (!(info->flags & FBINFO_HWACCEL_DISABLED)) {
		/* Make sure engine is reset */
		radeon_engine_idle();
		radeonfb_engine_reset(rinfo);
		radeon_engine_idle();
	}

	/* Blank display and LCD */
	radeonfb_blank(VESA_POWERDOWN, info);

	/* Sleep */
	rinfo->asleep = 1;
	rinfo->lock_blank = 1;

	/* Suspend the chip to D2 state when supported
	 */
#ifdef CONFIG_RADEON_HAS_D2
	if (radeon_suspend_to_d2(rinfo, state))
		radeon_set_suspend(rinfo, 1);
#endif /* CONFIG_RADEON_HAS_D2 */

	release_console_sem();

	pdev->dev.power_state = state;

	return 0;
}

int radeonfb_pci_resume(struct pci_dev *pdev)
{
        struct fb_info *info = pci_get_drvdata(pdev);
        struct radeonfb_info *rinfo = info->par;

	if (pdev->dev.power_state == 0)
		return 0;

	acquire_console_sem();

	/* Wakeup chip */
#ifdef CONFIG_RADEON_HAS_D2
	if (radeon_suspend_to_d2(rinfo, 0))
		radeon_set_suspend(rinfo, 0);
#endif /* CONFIG_RADEON_HAS_D2 */

	rinfo->asleep = 0;

	/* Restore display & engine */
	radeonfb_set_par(info);
	fb_pan_display(info, &info->var);
	fb_set_cmap(&info->cmap, info);

	/* Refresh */
	fb_set_suspend(info, 0);

	/* Unblank */
	rinfo->lock_blank = 0;
	radeonfb_blank(0, info);

	release_console_sem();

	pdev->dev.power_state = 0;

	printk(KERN_DEBUG "radeonfb: resumed !\n");

	return 0;
}

#endif /* CONFIG_PM */
