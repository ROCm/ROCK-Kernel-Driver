/*
 * linux/arch/arm/mach-omap/mux.c
 *
 * Utility to set the Omap MUX and PULL_DWN registers from a table in mux.h
 *
 * Copyright (C) 2003 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#define __MUX_C__
#include <asm/arch/mux.h>

/*
 * Sets the Omap MUX and PULL_DWN registers based on the table
 */
int omap_cfg_reg(const reg_cfg_t reg_cfg)
{
#ifdef CONFIG_OMAP_MUX
	static spinlock_t mux_spin_lock = SPIN_LOCK_UNLOCKED;

	unsigned long flags;
	reg_cfg_set *cfg;
	unsigned int reg_orig = 0, reg = 0, pu_pd_orig = 0, pu_pd = 0,
		pull_orig = 0, pull = 0;

	cfg = &reg_cfg_table[reg_cfg];

	/*
	 * We do a pretty long section here with lock on, but pin muxing
	 * should only happen on driver init for each driver, so it's not time
	 * critical.
	 */
	spin_lock_irqsave(&mux_spin_lock, flags);

	/* Check the mux register in question */
	if (cfg->mux_reg) {
		reg_orig = omap_readl(cfg->mux_reg);

		/* The mux registers always seem to be 3 bits long */
		reg = reg_orig & ~(0x7 << cfg->mask_offset);

		reg |= (cfg->mask << cfg->mask_offset);

		omap_writel(reg, cfg->mux_reg);
	}

	/* Check for pull up or pull down selection on 1610 */
	if (!cpu_is_omap1510()) {
		if (cfg->pu_pd_reg && cfg->pull_val) {
			pu_pd_orig = omap_readl(cfg->pu_pd_reg);
			if (cfg->pu_pd_val) {
				/* Use pull up */
				pu_pd = pu_pd_orig | (1 << cfg->pull_bit);
			} else {
				/* Use pull down */
				pu_pd = pu_pd_orig & ~(1 << cfg->pull_bit);
			}
			omap_writel(pu_pd, cfg->pu_pd_reg);
		}
	}

	/* Check for an associated pull down register */
	if (cfg->pull_reg) {
		pull_orig = omap_readl(cfg->pull_reg);

		if (cfg->pull_val) {
			/* Low bit = pull enabled */
			pull = pull_orig & ~(1 << cfg->pull_bit);
		} else {
			/* High bit = pull disabled */
			pull = pull_orig | (1 << cfg->pull_bit);
		}

		omap_writel(pull, cfg->pull_reg);
	}

#ifdef CONFIG_OMAP_MUX_DEBUG
	if (cfg->debug) {
		printk("Omap: Setting register %s\n", cfg->name);
		printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
		       cfg->mux_reg_name, cfg->mux_reg, reg_orig, reg);

		if (!cpu_is_omap1510()) {
			if (cfg->pu_pd_reg && cfg->pull_val) {
				printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
				       cfg->pu_pd_name, cfg->pu_pd_reg,
				       pu_pd_orig, pu_pd);
			}
		}

		if (cfg->pull_reg)
			printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
			       cfg->pull_name, cfg->pull_reg, pull_orig, pull);
	}
#endif

	spin_unlock_irqrestore(&mux_spin_lock, flags);

#endif
	return 0;
}

EXPORT_SYMBOL(omap_cfg_reg);
