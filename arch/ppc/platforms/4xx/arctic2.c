/*
 * arch/ppc/platforms/arctic2.c  Platform setup for the IBM Arctic-2 reference platform
 *					with the Subzero core card and Beech personality card
 * 				      Based on beech.c by Bishop Brock 
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
 * Ken Inoue 
 * IBM Thomas J. Watson Research Center
 * keninoue@us.ibm.com
 *
 * David Gibson
 * IBM Ozlabs, Canberra, Australia
 * arctic@gibson.dropbear.id.au
 */

#include <linux/blk.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/rtc.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/time.h>

#include <platforms/4xx/arctic2.h>

/* Virtual address of the PCCF macro, which needs to be ioremap()ed
 * and initialized by the board setup code. */
volatile u16 *pccf_4xx_macro_vaddr;
unsigned long pccf_4xx_io_base;
unsigned long pccf_4xx_mem_base;
EXPORT_SYMBOL(pccf_4xx_macro_vaddr);
EXPORT_SYMBOL(pccf_4xx_io_base);
EXPORT_SYMBOL(pccf_4xx_mem_base);

volatile u8 *arctic2_fpga_regs;
EXPORT_SYMBOL(arctic2_fpga_regs);

/* Different Arctic2 versions have different capabilities in terms of dynamic
   and static power control.  Older units do not support the APM peripheral or
   voltage scaling. The unit's capabilities are determined at boot and recorded
   in these variables.  Run-time rather than compile-time control is used to
   simplify kernel distribution. */

int arctic2_supports_apm = 0;
int arctic2_supports_dvs = 0;

#define GPIO0_OR 	((u32 *)(GPIO0_BASE + 0))
#define GPIO0_TCR	((u32 *)(GPIO0_BASE + 4))
#define GPIO0_ODR	((u32 *)(GPIO0_BASE + 8))
#define GPIO0_IR	((u32 *)(GPIO0_BASE + 12))

void __init
board_setup_irq(void)
{
	ibm405lp_setup_apm_pic();

	/*
	 * Set USB interrupt edge-triggered polarity=rising edge.
	 */

	mtdcr(DCRN_UIC0_TR, mfdcr(DCRN_UIC0_TR) | (1 << (31 - UIC_IRQ_EIR0)));
	mtdcr(DCRN_UIC0_PR, mfdcr(DCRN_UIC0_PR) | (1 << (31 - UIC_IRQ_EIR0)));
}

void
arctic2_poweroff(void)
{
	if (! arctic2_fpga_regs)
		BUG();

	cli();

	writeb(1, ARCTIC2_FPGA_POWERDOWN);
	eieio();

	while (1)
		;
}

void
arctic2_set_lcdpower(int on)
{
	iobarrier_rw(); 
	if (on)
		out_be32(GPIO0_TCR, in_be32(GPIO0_TCR) | 0x80000000 );
	else
		out_be32(GPIO0_TCR, in_be32(GPIO0_TCR) & ~0x80000000);
	iobarrier_rw(); 
	udelay(100); /* KI guard time */
}

EXPORT_SYMBOL(arctic2_poweroff);
EXPORT_SYMBOL(arctic2_set_lcdpower);

/* Units that support APM/DVS pull GPIO3 low as a strap. On older units this
   GPIO is pulled high. After boot this can be used as a trace/debug signal, as
   it has no other purpose on the board. */

static void __init
check_apm_dvs_support(void)
{
	u32 gpio3 = 0x10000000;

	iobarrier_rw();
	out_be32(GPIO0_TCR, in_be32(GPIO0_TCR) & ~gpio3);
	out_be32(GPIO0_ODR, in_be32(GPIO0_ODR) & ~gpio3);
	iobarrier_rw();
	arctic2_supports_apm = ((in_be32(GPIO0_IR) & gpio3) == 0);
	arctic2_supports_dvs = arctic2_supports_apm;
}

void __init
arctic_setup_arch(void)
{
	cpc0_cgcr1_t	cgcr1;
	u32 cfg;

	ppc4xx_setup_arch();

#ifdef CONFIG_GEN_RTC
	ppc_md.time_init = ibm405lp_time_init;
	ppc_md.set_rtc_time = ibm405lp_set_rtc_time;
	ppc_md.get_rtc_time = ibm405lp_get_rtc_time;
#endif
	ppc_md.power_off = arctic2_poweroff;

	/* Set up the EBC, then Disable the LCD controller, which may have been
	   left on by the BIOS. */

	subzero_core_ebc_setup();

	/* Turn on PerClk, so that the SDIO chip works */
	/* FIXME: This is bad for power usage - this will want to be
	 * fixed to turn the clock on "on demand" when we merge with
	 * the DPM code. */
	cgcr1.reg=mfdcr(DCRN_CPC0_CGCR1);
	cgcr1.fields.csel=CPC0_CGCR1_CSEL_PERCLK;
	mtdcr(DCRN_CPC0_CGCR1, cgcr1.reg);

	/* Configure the Arctic-II specific EBC banks */

	/* Bank 1: 16-bit FPGA peripherals (ethernet data, SDIO, USB, DOC)
	 * 1MB, RW, 16-bit at 0xf9000000-0xf90fffff */
	/* The access parameters are programmed assuming a 33Mhz EBC
	   clock, which is true for nearly all the operating points we
	   have defined:
	   	BME=0, TWT=5, CSN=0, OEN=1, WBN=1, WBF=1 TH=4
		RE=1, SOR=0, BEM=0, PEN=0
	 */
	mtdcri(DCRN_EBC0, BnAP(1), 0x02815900);
	mtdcri(DCRN_EBC0, BnCR(1), ARCTIC2_FPGA16_PADDR | 0x1a000);

	/* Bank 2: 8-bit FPGA peripherals (switch/control, ethernet regs, TCPA)
	 * 1MB, RW, 8-bit at 0xf8000000-0xf80fffff */
	mtdcri(DCRN_EBC0, BnAP(2), 0x02815580);
	mtdcri(DCRN_EBC0, BnCR(2), ARCTIC2_FPGA8_PADDR | 0x18000);

	mtdcri(DCRN_LCD0, DER, 0);

	/* Data access of the Arctic2 debug sled ethernet chip will time out
	   under certain conditions unless the EBC ready wait is extended. The
	   data sheet doesn't give a bound on this, so we allow a generous
	   amount of time. Note that this problem is normally masked by the
	   PCMCIA setup, which sets an even longer timeout. */

	cfg = mfdcri(DCRN_EBC0, CFG);
	if ((cfg & EBC_CFG_RTC) < EBC_CFG_RTC_128)
		mtdcri(DCRN_EBC0, CFG, (cfg & ~EBC_CFG_RTC) | EBC_CFG_RTC_128);
}


void __init
arctic_map_io(void)
{
	ppc4xx_map_io();
	ibm405lp_setup_pccf(&pccf_4xx_macro_vaddr, &pccf_4xx_io_base,
			    &pccf_4xx_mem_base);

#if 0
	if (! request_mem_region(ARCTIC2_FPGA8_PADDR,
				 ARCTIC2_FPGA_REGS_EXTENT,
				 "Arctic-2 FPGA Control Registers"))
		BUG(); /* If someone's grabbed these addresses
			* already, something's seriously wrong */
#endif

	arctic2_fpga_regs = ioremap(ARCTIC2_FPGA8_PADDR,
				    ARCTIC2_FPGA_REGS_EXTENT);
	if (!arctic2_fpga_regs)
		BUG();

	check_apm_dvs_support();
	return;
}


void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = arctic_setup_arch;
	ppc_md.setup_io_mappings = arctic_map_io;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End: */
