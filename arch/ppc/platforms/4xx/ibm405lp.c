/*
 * arch/ppc/platforms/ibm405lp.c  405LP-specific code
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * Bishop Brock
 * IBM Research, Austin Center for Low-Power Computing
 * bcbrock@us.ibm.com
 * March, 2002
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/string.h>

#include <asm/delay.h>
#include <asm/hardirq.h>
#include <asm/ibm4xx.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/uaccess.h>
#include <asm/ocp.h>

struct ocp_def core_ocp[]  __initdata = {
	{OCP_VENDOR_IBM, OCP_FUNC_OPB, OPB0_BASE, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART0_IO_BASE, UART0_INT,IBM_CPM_UART0},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART1_IO_BASE, UART1_INT, IBM_CPM_UART1},
	{OCP_VENDOR_IBM, OCP_FUNC_IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{OCP_VENDOR_IBM, OCP_FUNC_GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OCP_VENDOR_INVALID, OCP_FUNC_INVALID, 0x0, OCP_IRQ_NA, OCP_CPM_NA},
};

#ifdef CONFIG_PM
/* Set up the 405LP clock and power management unit for aggressive power
   management.

   Briefly, there are 3 CPM "classes":

   Class 1 - Either completely asleep or awake.  The "force" state is
             equivalent to the "enabled" state.  Many Class 1 units are
             critical system components and are never power managed.

   Class 2 - Can be enabled for power management, where sleep requests are
             made by the peripheral, typically after an inactivity timeout.
	     When sleeping, critical interfaces remain active, and
	     awaken the unit whenever it is targeted with a transaction.

   Class 3 - Can be enabled for power management, where sleep requests are
             made by the CPM.  Power management for these units typically
             will require intelligence in a device driver.

   In the current implementation, the "force" bits are only used on Class 1
   devices, and only when the associated driver has the intelligence necessary
   to "unforce" the power management state.  A previous scheme, which tried to
   enable power management based on whether a particular driver was compiled
   with the kernel, caused many problems and is never used here.

   Class 2 devices with timeouts are normally initialized for the most
   aggressive values.  There is no power management benefit of "forcing" Class
   2 devices over letting their inactivity timeouts take effect.  Therefore,
   after being set up here, Class 2 device drivers don't need to worry about
   CPM.

   No Class 3 devices are handled yet.  */

void __init
ibm405lp_setup_cpm(void)
{
	u32 force = 0;
	u32 enable = 0;
	dma0_slp_t dma0_slp;
	dcp0_cfg_t dcp0_cfg;
	ebc0_cfg_t ebc0_cfg;
	sdram0_cfg_t sdram0_cfg;
	sdram0_pmit_t sdram0_pmit;
	sla0_slpmd_t sla0_slpmd;

	/* Initialize the CPM state */

	mtdcr(DCRN_CPMFR, force);
	mtdcr(DCRN_CPMER, enable);

	/* IIC - Class 3 - Not handled yet.  The driver should at least be able
	   to force/unforce itself. */

	/* CPU - class 2 - There doesn't appear to be a timeout associated with
	   this, and the exact function is not documented anywhere.  It saves a
	   lot of power, though. I assume this gates core clocks when the CPU
	   core is asleep, and probably adds a couple of cycles of latency when
	   the CPU core wakes up. */

	enable |= IBM_CPM_CPU;

	/* DMA - class 2. Set for the minimum timeout, which is 32 cycles. */

	dma0_slp.reg = mfdcr(DCRN_SLP);
	dma0_slp.fields.sme = 1;
	dma0_slp.fields.idu = 0;
	mtdcr(DCRN_SLP, dma0_slp.reg);
	enable |= IBM_CPM_DMA;

	/* BRG - Class 2.  Seems to crash the system when enabled in 405LP Pass
	   1

	   DCP (CodePack) - Class 2.  The semantics of the sleep delay are not
	   documented. We'll use 32 (what the heck). */

	dcp0_cfg.reg = mfdcri(DCRN_DCP0, CFG);
	dcp0_cfg.fields.slen = 1;
	dcp0_cfg.fields.sldy = 32;
	mtdcri(DCRN_DCP0, CFG, dcp0_cfg.reg);
	enable |= IBM_CPM_DCP;

	/* EBC - Class 2.  Set for minimum timeout, which is 32 cycles. [ I
	   think this is 32. It may be 64. I don't trust the documentation. ]
	 */

	ebc0_cfg.reg = mfdcri(DCRN_EBC0, CFG);
	ebc0_cfg.fields.pme = 1;
	ebc0_cfg.fields.pmt = 1;
	mtdcri(DCRN_EBC0, CFG, ebc0_cfg.reg);
	enable |= IBM_CPM_EBC;

	/* SDRAM - Class 2.  Set for the minimum 32-cycle timeout.

	   The documentation on this core is clear - waking a sleeping SDRAM
	   controller takes 2 PLB cycles, which is added to the latency of the
	   memory operation. If someone can prove that this is affecting
	   performance we can easily back this off.  */

	sdram0_cfg.reg = mfdcri(DCRN_SDRAM0, CFG);
	sdram0_cfg.fields.pme = 1;
	mtdcri(DCRN_SDRAM0, CFG, sdram0_cfg.reg);

	sdram0_pmit.reg = mfdcri(DCRN_SDRAM0, PMIT);
	sdram0_pmit.fields.cnt = 0;
	mtdcri(DCRN_SDRAM0, PMIT, sdram0_pmit.reg);

	enable |= IBM_CPM_SDRAM0;

	/* PLB - Class 2.  Seems to crash the system when enabled in 405LP Pass
	   1.

	   GPIO - Class 1.  This unit is used for many things, and no single
	   driver controls all GPIO.  It's best left unmanaged (it doesn't use
	   much power anyway). NB: 405LP Pass 1 erratum - forcing PM on GPIO
	   kills the TPC.

	   UART0 - Class 1
	   UART1 - Class 1

	   Someone should work on the serial port drivers to enable PM support
	   for them. Any takers?

	   UIC        - Class 1
	   CPU_TMRCLK - Class 1

	   These system resources are never power managed.  */

	/* SLA - Class 2.  Set for the minimum 32-cycle timeout. */

	sla0_slpmd.reg = mfdcri(DCRN_SLA0, SLPMD);
	sla0_slpmd.fields.slen = 1;
	sla0_slpmd.fields.slcr = 0;
	mtdcri(DCRN_SLA0, SLPMD, sla0_slpmd.reg);
	enable |= IBM_CPM_SLA;

	/* CSI  - Class 1.
	   TPC  - Class 1.
	   TDES - Class 1.

	   The drivers for these units are power-aware, and manage the device
	   properly. By default these units are forced off at boot. */

	force |= IBM_CPM_CSI;
	force |= IBM_CPM_TPC;
	force |= IBM_CPM_TDES;

	/* Set the CPM state */

	mtdcr(DCRN_CPMFR, force);
	mtdcr(DCRN_CPMER, enable);
}
#endif

/* This routine is included here because the framebuffer driver needs a way to
   tell the system the Pixel clock frequency it needs, regardless of whether
   run-time frequency scaling is configured.  A hook and a couple of global
   variables are always present and will be used by the RTVFS driver if it is
   loaded.

   Pixel clock setting is kind of a hack, as the frequency steps available from
   the PLB/PixClk divider may be too large to guarantee that we'll hit within
   the given limits.  We never set the frequency above the upper bound, but due
   to quantization may need to set the frequency below the lower bound.  So far
   it works OK for the panels we've tried.

   In general, the choice of a system clock frequency should be made with
   consideration of the LCD panel to be attached, to guarantee a good clock
   divider for the Pixel clock regardless of frequency scaling.

   Clock frequencies are in KHz. If pixclk_min or pixclk_max are zero, we set
   the lowest possible frequency to conserve energy. */

int (*set_pixclk_hook) (unsigned pixclk_min, unsigned pixclk_max) = NULL;
unsigned last_pixclk_min = 0;
unsigned last_pixclk_max = 0;

EXPORT_SYMBOL(set_pixclk_hook);
EXPORT_SYMBOL(last_pixclk_min);
EXPORT_SYMBOL(last_pixclk_max);

int
ibm405lp_set_pixclk(unsigned pixclk_min, unsigned pixclk_max)
{
	unsigned divider;
	bd_t *bip = (bd_t *) __res;
	unsigned plb_khz = bip->bi_busfreq / 1000;
	cpc0_cgcr1_t cgcr1;

	if (set_pixclk_hook) {
		return (set_pixclk_hook) (pixclk_min, pixclk_max);
	} else {
		if ((pixclk_min == 0) || (pixclk_max == 0))
			divider = CPC0_DIV_MAX;
		else {
			divider = plb_khz / pixclk_min;
			if (divider == 0)
				divider = 1;
			if ((divider < CPC0_DIV_MAX) &&
			    ((plb_khz / divider) > pixclk_max))
				divider++;
		}

		cgcr1.reg = mfdcr(DCRN_CPC0_CGCR1);
		cgcr1.fields.ppxl = CPC0_DIV_ENCODE(divider);
		mtdcr(DCRN_CPC0_CGCR1, cgcr1.reg);

		last_pixclk_min = pixclk_min;
		last_pixclk_max = pixclk_max;
		return 0;
	}
}
