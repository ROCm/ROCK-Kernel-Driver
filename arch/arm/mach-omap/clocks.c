/*
 * Clock interface for OMAP
 *
 * Copyright (C) 2001 RidgeRun, Inc
 * Written by Gordon McNutt <gmcnutt@ridgerun.com>
 * Updated 2004 for Linux 2.6 by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/errno.h>
#include <asm/io.h>
#include <asm/arch/clocks.h>

/* Input clock in MHz */
static unsigned int source_clock = 12;

/*
 * We use one spinlock for all clock registers for now. We may want to
 * change this to be clock register specific later on. Before we can do
 * that, we need to map out the shared clock registers.
 */
static spinlock_t clock_lock = SPIN_LOCK_UNLOCKED;

typedef struct {
	char		*name;
	__u8		flags;
	ck_t		parent;
	volatile __u16	*rate_reg;	/* Clock rate register */
	volatile __u16	*enbl_reg;	/* Enable register */
	volatile __u16	*idle_reg;	/* Idle register */
	volatile __u16	*slct_reg;	/* Select register */
	__s8		rate_shift;	/* Clock rate bit shift */
	__s8		enbl_shift;	/* Clock enable bit shift */
	__s8		idle_shift;	/* Clock idle bit shift */
	__s8		slct_shift;	/* Clock select bit shift */
} ck_info_t;

#define CK_NAME(ck)		ck_info_table[ck].name
#define CK_FLAGS(ck)		ck_info_table[ck].flags
#define CK_PARENT(ck)		ck_info_table[ck].parent
#define CK_RATE_REG(ck)		ck_info_table[ck].rate_reg
#define CK_ENABLE_REG(ck)	ck_info_table[ck].enbl_reg
#define CK_IDLE_REG(ck)		ck_info_table[ck].idle_reg
#define CK_SELECT_REG(ck)	ck_info_table[ck].slct_reg
#define CK_RATE_SHIFT(ck)	ck_info_table[ck].rate_shift
#define CK_ENABLE_SHIFT(ck)	ck_info_table[ck].enbl_shift
#define CK_IDLE_SHIFT(ck)	ck_info_table[ck].idle_shift
#define CK_SELECT_SHIFT(ck)	ck_info_table[ck].slct_shift
#define CK_CAN_CHANGE_RATE(cl)	(CK_FLAGS(ck) & CK_RATEF)
#define CK_CAN_DISABLE(cl)	(CK_FLAGS(ck) & CK_ENABLEF)
#define CK_CAN_IDLE(cl)		(CK_FLAGS(ck) & CK_IDLEF)
#define CK_CAN_SWITCH(cl)	(CK_FLAGS(ck) & CK_SELECTF)

static ck_info_t ck_info_table[] = {
	{
		.name		= "clkin",
		.flags		= 0,
		.parent		= OMAP_CLKIN,
	}, {
		.name		= "ck_gen1",
		.flags		= CK_RATEF | CK_IDLEF,
		.rate_reg	= CK_DPLL1,
		.idle_reg	= ARM_IDLECT1,
		.idle_shift	= IDLDPLL_ARM,
		.parent		= OMAP_CLKIN,
	}, {
		.name		= "ck_gen2",
		.flags		= 0,
		.parent		= OMAP_CK_GEN1,
	}, {
		.name		= "ck_gen3",
		.flags		= 0,
		.parent		= OMAP_CK_GEN1,
	}, {
		.name		= "tc_ck",
		.flags		= CK_RATEF | CK_IDLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.idle_reg	= ARM_IDLECT1,
		.rate_shift	= TCDIV,
		.idle_shift	= IDLIF_ARM
	}, {
		.name		= "arm_ck",
		.flags		= CK_IDLEF | CK_RATEF,
		.parent		= OMAP_CK_GEN1,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[ARMDIV(5:4)] */
		.idle_reg	= ARM_IDLECT1,
		.rate_shift	= ARMDIV,
		.idle_shift	= SETARM_IDLE,
	}, {
		.name		= "mpuper_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN1,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[PERDIV(1:0)] */
		.enbl_reg	= ARM_IDLECT2,
		.idle_reg	= ARM_IDLECT1,
		.rate_shift	= PERDIV,
		.enbl_shift	= EN_PERCK,
		.idle_shift	= IDLPER_ARM
	}, {
		.name		= "arm_gpio_ck",
		.flags		= CK_ENABLEF,
		.parent		= OMAP_CK_GEN1,
		.enbl_reg	= ARM_IDLECT2,
		.enbl_shift	= EN_GPIOCK
	}, {
		.name		= "mpuxor_ck",
		.flags		= CK_ENABLEF | CK_IDLEF,
		.parent		= OMAP_CLKIN,
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.idle_shift	= IDLXORP_ARM,
		.enbl_shift	= EN_XORPCK
	}, {
		.name		= "mputim_ck",
		.flags		= CK_IDLEF | CK_ENABLEF | CK_SELECTF,
		.parent		= OMAP_CLKIN,
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.slct_reg	= ARM_CKCTL,
		.idle_shift	= IDLTIM_ARM,
		.enbl_shift	= EN_TIMCK,
		.slct_shift	= ARM_TIMXO
	}, {
		.name		= "mpuwd_ck",
		.flags		= CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CLKIN,
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.idle_shift	= IDLWDT_ARM,
		.enbl_shift	= EN_WDTCK,
	}, {
		.name		= "dsp_ck",
		.flags		= CK_RATEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN2,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[DSPDIV(7:6)] */
		.enbl_reg	= ARM_CKCTL,
		.rate_shift	= DSPDIV,
		.enbl_shift	= EN_DSPCK,
	}, {
		.name		= "dspmmu_ck",
		.flags		= CK_RATEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN2,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[DSPMMUDIV(11:10)] */
		.enbl_reg	= ARM_CKCTL,
		.rate_shift	= DSPMMUDIV,
		.enbl_shift	= EN_DSPCK,
	}, {
		.name		= "dma_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= TCDIV,
		.idle_shift	= IDLIF_ARM,
		.enbl_shift	= DMACK_REQ
	}, {
		.name		= "api_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= TCDIV,
		.idle_shift	= IDLAPI_ARM,
		.enbl_shift	= EN_APICK,
	}, {
		.name		= "hsab_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= TCDIV,
		.idle_shift	= IDLHSAB_ARM,
		.enbl_shift	= EN_HSABCK,
	}, {
		.name		= "lbfree_ck",
		.flags		= CK_RATEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= TCDIV,
		.enbl_shift	= EN_LBFREECK,
	}, {
		.name		= "lb_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[TCDIV(9:8)] */
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= TCDIV,
		.idle_shift	= IDLLB_ARM,
		.enbl_shift	= EN_LBCK,
	}, {
		.name		= "lcd_ck",
		.flags		= CK_RATEF | CK_IDLEF | CK_ENABLEF,
		.parent		= OMAP_CK_GEN3,
		.rate_reg	= ARM_CKCTL,	/* ARM_CKCTL[LCDDIV(3:2)] */
		.idle_reg	= ARM_IDLECT1,
		.enbl_reg	= ARM_IDLECT2,
		.rate_shift	= LCDDIV,
		.idle_shift	= IDLLCD_ARM,
		.enbl_shift	= EN_LCDCK,
	},
};

/*****************************************************************************/

#define CK_IN_RANGE(ck)		(!((ck < OMAP_CK_MIN) || (ck > OMAP_CK_MAX)))

int ck_auto_unclock = 1;
int ck_debug = 0;

#define CK_MAX_PLL_FREQ		OMAP_CK_MAX_RATE
static __u8 ck_valid_table[CK_MAX_PLL_FREQ / 8 + 1];
static __u8 ck_lookup_table[CK_MAX_PLL_FREQ];

int
ck_set_input(ck_t ck, ck_t input)
{
	int ret = 0, shift;
	volatile __u16 *reg;
	unsigned long flags;

	if (!CK_IN_RANGE(ck) || !CK_CAN_SWITCH(ck)) {
		ret = -EINVAL;
		goto exit;
	}

	reg = CK_SELECT_REG(ck);
	shift = CK_SELECT_SHIFT(ck);

	spin_lock_irqsave(&clock_lock, flags);
	if (input == OMAP_CLKIN) {
		*((volatile __u16 *) reg) &= ~(1 << shift);
		goto exit;
	} else if (input == CK_PARENT(ck)) {
		*((volatile __u16 *) reg) |= (1 << shift);
		goto exit;
	}

	ret = -EINVAL;
 exit:
	spin_unlock_irqrestore(&clock_lock, flags);
	return ret;
}

int
ck_get_input(ck_t ck, ck_t * input)
{
	int ret = -EINVAL;
	unsigned long flags;

	if (!CK_IN_RANGE(ck))
		goto exit;

	ret = 0;

	spin_lock_irqsave(&clock_lock, flags);
	if (CK_CAN_SWITCH(ck)) {
		int shift;
		volatile __u16 *reg;

		reg = CK_SELECT_REG(ck);
		shift = CK_SELECT_SHIFT(ck);
		if (*reg & (1 << shift)) {
			*input = CK_PARENT(ck);
			goto exit;
		}
	}

	*input = OMAP_CLKIN;

 exit:
	spin_unlock_irqrestore(&clock_lock, flags);
	return ret;
}

static int
__ck_set_pll_rate(ck_t ck, int rate)
{
	volatile __u16 *pll;
	unsigned long flags;

	if ((rate < 0) || (rate > CK_MAX_PLL_FREQ))
		return -EINVAL;

	/* Scan downward for the closest matching frequency */
	while (rate && !test_bit(rate, (unsigned long *)&ck_valid_table))
		rate--;

	if (!rate) {
		printk(KERN_ERR "%s: couldn't find a matching rate\n",
			__FUNCTION__);
		return -EINVAL;
	}

	spin_lock_irqsave(&clock_lock, flags);
	pll = (volatile __u16 *) CK_RATE_REG(ck);

	/* Clear the rate bits */
	*pll &= ~(0x1f << 5);

	/* Set the rate bits */
	*pll |= (ck_lookup_table[rate - 1] << 5);
	spin_unlock_irqrestore(&clock_lock, flags);

	return 0;
}

static int
__ck_set_clkm_rate(ck_t ck, int rate)
{
	int shift, prate, div, ret;
	volatile __u16 *reg;
	unsigned long flags;

	spin_lock_irqsave(&clock_lock, flags);

	/*
	 * We can only set this clock's value to a fraction of its
	 * parent's value. The interface says I'll round down when necessary.
	 * So first let's get the parent's current rate.
	 */
	prate = ck_get_rate(CK_PARENT(ck));

	/*
	 * Let's just start with the highest fraction and keep searching
	 * down through available rates until we find one less than or equal
	 * to the desired rate.
	 */
	for (div = 0; div < 4; div++) {
		if (prate <= rate)
			break;
		prate = prate / 2;
	}

	/*
	 * Oops. Looks like the caller wants a rate lower than we can support.
	 */
	if (div == 5) {
		printk(KERN_ERR "%s: %d is too low\n",
			__FUNCTION__, rate);
		ret = -EINVAL;
		goto exit;
	}

	/*
	 * One more detail: if this clock supports more than one parent, then
	 * we're going to automatically switch over to the parent which runs
	 * through the divisor. For omap this is not ambiguous because for all
	 * such clocks one choice is always OMAP_CLKIN (which doesn't run
	 * through the divisor) and the other is whatever I encoded as
	 * CK_PARENT. Note that I wait until we get this far because I don't
	 * want to switch the input until we're sure this is going to work.
	 */
	if (CK_CAN_SWITCH(ck))
		if ((ret = ck_set_input(ck, CK_PARENT(ck))) < 0) {
			BUG();
			goto exit;
		}

	/*
	 * At last, we can set the divisor. Clear the old rate bits and
	 * set the new ones.
	 */
	reg = (volatile __u16 *) CK_RATE_REG(ck);
	shift = CK_RATE_SHIFT(ck);
	*reg &= ~(3 << shift);
	*reg |= (div << shift);

	/* And return the new (actual, after rounding down) rate. */
	ret = prate;

 exit:
	spin_unlock_irqrestore(&clock_lock, flags);
	return ret;
}

int
ck_set_rate(ck_t ck, int rate)
{
	int ret = -EINVAL;

	if (!CK_IN_RANGE(ck) || !CK_CAN_CHANGE_RATE(ck))
		goto exit;

	switch (ck) {

	default:
		ret = __ck_set_clkm_rate(ck, rate);
		break;

	case OMAP_CK_GEN1:
		ret = __ck_set_pll_rate(ck, rate);
		break;

	};

 exit:
	return ret;
}

static int
__ck_get_pll_rate(ck_t ck)
{
	int m, d;

	__u16 pll = *((volatile __u16 *) CK_RATE_REG(ck));

	m = (pll & (0x1f << 7)) >> 7;
	m = m ? m : 1;
	d = (pll & (3 << 5)) >> 5;
	d++;

	return ((source_clock * m) / d);
}

static int
__ck_get_clkm_rate(ck_t ck)
{
	static int bits2div[] = { 1, 2, 4, 8 };
	int in, bits, reg, shift;

	reg = *(CK_RATE_REG(ck));
	shift = CK_RATE_SHIFT(ck);

	in = ck_get_rate(CK_PARENT(ck));
	bits = (reg & (3 << shift)) >> shift;

	return (in / bits2div[bits]);
}

int
ck_get_rate(ck_t ck)
{
	int ret = 0;
	ck_t parent;

	if (!CK_IN_RANGE(ck)) {
		ret = -EINVAL;
		goto exit;
	}

	switch (ck) {

	case OMAP_CK_GEN1:
		ret = __ck_get_pll_rate(ck);
		break;

	case OMAP_CLKIN:
		ret = source_clock;
		break;

	case OMAP_MPUXOR_CK:
	case OMAP_CK_GEN2:
	case OMAP_CK_GEN3:
	case OMAP_ARM_GPIO_CK:
		ret = ck_get_rate(CK_PARENT(ck));
		break;

	case OMAP_ARM_CK:
	case OMAP_MPUPER_CK:
	case OMAP_DSP_CK:
	case OMAP_DSPMMU_CK:
	case OMAP_LCD_CK:
	case OMAP_TC_CK:
	case OMAP_DMA_CK:
	case OMAP_API_CK:
	case OMAP_HSAB_CK:
	case OMAP_LBFREE_CK:
	case OMAP_LB_CK:
		ret = __ck_get_clkm_rate(ck);
		break;

	case OMAP_MPUTIM_CK:
		ck_get_input(ck, &parent);
		ret = ck_get_rate(parent);
		break;

	case OMAP_MPUWD_CK:
		/* Note that this evaluates to zero if source_clock is 12MHz. */
		ret = source_clock / 14;
		break;
	default:
		ret = -EINVAL;
		break;
	}

 exit:
	return ret;
}

int
ck_enable(ck_t ck)
{
	volatile __u16 *reg;
	int ret = -EINVAL, shift;
	unsigned long flags;

	if (!CK_IN_RANGE(ck))
		goto exit;

	if (ck_debug)
		printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, CK_NAME(ck));

	ret = 0;

	if (!CK_CAN_DISABLE(ck))
		/* Then it must be on... */
		goto exit;

	spin_lock_irqsave(&clock_lock, flags);
	reg = CK_ENABLE_REG(ck);
	shift = CK_ENABLE_SHIFT(ck);
	*reg |= (1 << shift);
	spin_unlock_irqrestore(&clock_lock, flags);

 exit:
	return ret;
}

int
ck_disable(ck_t ck)
{
	volatile __u16 *reg;
	int ret = -EINVAL, shift;
	unsigned long flags;

	if (!CK_IN_RANGE(ck))
		goto exit;

	if (ck_debug)
		printk(KERN_DEBUG "%s: %s\n", __FUNCTION__, CK_NAME(ck));

	if (!CK_CAN_DISABLE(ck))
		goto exit;

	ret = 0;

	if (ck == OMAP_CLKIN)
		return -EINVAL;

	spin_lock_irqsave(&clock_lock, flags);
	reg = CK_ENABLE_REG(ck);
	shift = CK_ENABLE_SHIFT(ck);
	*reg &= ~(1 << shift);
	spin_unlock_irqrestore(&clock_lock, flags);

 exit:
	return ret;
}

int ck_valid_rate(int rate)
{
	return test_bit(rate, (unsigned long *)&ck_valid_table);
}

static void
__ck_make_lookup_table(void)
{
	__u8 m, d;

	memset(ck_valid_table, 0, sizeof (ck_valid_table));

	for (m = 1; m < 32; m++)
		for (d = 1; d < 5; d++) {

			int rate = ((source_clock * m) / (d));

			if (rate > CK_MAX_PLL_FREQ)
				continue;
			if (test_bit(rate, (unsigned long *)&ck_valid_table))
				continue;
			set_bit(rate, (unsigned long *)&ck_valid_table);
			ck_lookup_table[rate - 1] = (m << 2) | (d - 1);
		}
}

int __init
init_ck(void)
{
	__ck_make_lookup_table();

	/* We want to be in syncronous scalable mode */
	*ARM_SYSST = 0x1000;
#if defined(CONFIG_OMAP_ARM_30MHZ)
	*ARM_CKCTL = 0x1555;
	*DPLL_CTL_REG = 0x2290;
#elif defined(CONFIG_OMAP_ARM_60MHZ)
	*ARM_CKCTL = 0x1005;
	*DPLL_CTL_REG = 0x2290;
#elif defined(CONFIG_OMAP_ARM_96MHZ)
	*ARM_CKCTL = 0x1005;
	*DPLL_CTL_REG = 0x2410;
#elif defined(CONFIG_OMAP_ARM_120MHZ)
	*ARM_CKCTL = 0x110a;
	*DPLL_CTL_REG = 0x2510;
#elif defined(CONFIG_OMAP_ARM_168MHZ)
	*ARM_CKCTL = 0x110f;
	*DPLL_CTL_REG = 0x2710;
#elif defined(CONFIG_OMAP_ARM_182MHZ) && defined(CONFIG_ARCH_OMAP730)
	*ARM_CKCTL = 0x250E;
	*DPLL_CTL_REG = 0x2713;
#elif defined(CONFIG_OMAP_ARM_192MHZ) && defined(CONFIG_ARCH_OMAP1610)
	*ARM_CKCTL = 0x110f;
	if (crystal_type == 2) {
		source_clock = 13;	/* MHz */
		*DPLL_CTL_REG = 0x2510;
	} else
		*DPLL_CTL_REG = 0x2810;
#elif defined(CONFIG_OMAP_ARM_195MHZ) && defined(CONFIG_ARCH_OMAP730)
	*ARM_CKCTL = 0x250E;
	*DPLL_CTL_REG = 0x2793;
#else
#error "OMAP MHZ not set, please run make xconfig"
#endif

	/* Turn off some other junk the bootloader might have turned on */
	*ARM_CKCTL &= 0x0fff;	/* Turn off DSP, ARM_INTHCK, ARM_TIMXO */
	*ARM_RSTCT1 = 0;	/* Put DSP/MPUI into reset until needed */
	*ARM_RSTCT2 = 1;
	*ARM_IDLECT1 = 0x400;

	/*
	 * According to OMAP5910 Erratum SYS_DMA_1, bit DMACK_REQ (bit 8)
	 * of the ARM_IDLECT2 register must be set to zero. The power-on
	 * default value of this bit is one.
	 */
	*ARM_IDLECT2 = 0x0000;	/* Turn LCD clock off also */

	/*
	 * Only enable those clocks we will need, let the drivers
	 * enable other clocks as necessary
	 */
	ck_enable(OMAP_MPUPER_CK);
	ck_enable(OMAP_ARM_GPIO_CK);
	ck_enable(OMAP_MPUXOR_CK);
	//ck_set_rate(OMAP_MPUTIM_CK, OMAP_CLKIN);
	ck_enable(OMAP_MPUTIM_CK);
	start_mputimer1(0xffffffff);

	return 0;
}


EXPORT_SYMBOL(ck_get_rate);
EXPORT_SYMBOL(ck_set_rate);
EXPORT_SYMBOL(ck_enable);
EXPORT_SYMBOL(ck_disable);
