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
#include <linux/interrupt.h>
#include <linux/bcd.h>

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
#include <asm/pccf_4xx.h>

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
#endif /* CONFIG_PM */

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
	bd_t *bip = &__res;
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

/* Somewhat misleading name, as well as the EBC, this sets up the UIC
   and CPC ready for PCMCIA operation */
static void
pccf_ebc_setup(void)
{
	/* Set up EBC bank 4 as per PCCF docs., assuming 66 MHz EBC bus. The
	   ready timeout is set for 1024 cycles (~ 15 us at 66 MHz), unless
	   someone else has already set it for 2048.  In the event of a
	   timeout we'll get a Data Machine Check. */

	unsigned long bits, mask, flags;

	save_flags(flags);
	cli();

	/* Program EBC0_CFG for ready timeout */

	mtdcr(DCRN_EBC0_CFGADDR, DCRN_EBC0_CFG);
	bits = mfdcr(DCRN_EBC0_CFGDATA);
	if ((bits & EBC_CFG_RTC) != EBC_CFG_RTC_2048)
		mtdcr(DCRN_EBC0_CFGDATA, (bits & ~EBC_CFG_RTC) | EBC_CFG_RTC_1024);

	/* Program EBC bank properties : 32 MB, 16-bit RW bank; 
	   BME = 0, TWT = 22, CSN = 2, OEN = 3, WBN = WBF = 0, TH = 5, 
	   RE = 1, SOR = 0, BEM = 1 */

	mtdcr(DCRN_EBC0_CFGADDR, DCRN_EBC0_B4CR);
	mtdcr(DCRN_EBC0_CFGDATA, (PCCF_4XX_PADDR & 0xfff00000) | 0x000ba000);
	mtdcr(DCRN_EBC0_CFGADDR, DCRN_EBC0_B4AP);
	mtdcr(DCRN_EBC0_CFGDATA, 0x0b0b0b40);

	/* Program the UIC for active-high, level-triggered interrupts.  Note
	   that the active-low PCMCIA interrupt pin is inverted by the PCCF
	   macro.  */

	mask = (0x80000000 >> PCCF_4XX_MACRO_IRQ) | 
		(0x80000000 >> PCCF_4XX_CARD_IRQ);

	bits = mfdcr(DCRN_UIC0_PR);
	bits |= mask;
	mtdcr(DCRN_UIC0_PR, bits);

	bits = mfdcr(DCRN_UIC0_TR);
	bits &= ~mask;
	mtdcr(DCRN_UIC0_TR, bits);

	/* Clear CPC0_CR0[PCMD] to enable the PCMCIA controller */

	mtdcr(DCRN_CPC0_CR0, mfdcr(DCRN_CPC0_CR0) & ~0x00000200);

	restore_flags(flags);
}

/* Map the PCCF controller's memory windows.
 *
 * HACK ALERT: Logically this belongs in the pccf_4xx driver itself,
 * however that causes problems because it happens so late in
 * initialization.  We want to use some ISA-ish drivers (notably
 * 8390.c) on memory mapped devices by using the
 * ioaddr=(memaddr-_IO_BASE) hack.  If _IO_BASE is the PCMCIA ISA IO
 * space (which we want so PC Card drivers using ISA IO work) but is
 * not initialized until the pccf_4xx driver starts, this could well
 * be after drivers like 8390 have initialized and computed a fake
 * "IO" address which is now incorrect.  Putting the ioremap()ing of
 * the PCCF macro in the chip/board setup code works around this
 * problem. */
int
ibm405lp_setup_pccf(volatile u16 **vaddr, unsigned long *io_base,
		    unsigned long *mem_base)
{
	pccf_ebc_setup();

	*vaddr = ioremap(PCCF_4XX_MACRO_PADDR, PCCF_4XX_MACRO_WINSIZE);

	if (*vaddr == NULL) {
		printk(KERN_ERR "pccf_4xx: ioremap macro at 0x%lx failed.\n",
		       PCCF_4XX_MACRO_PADDR);
		return -EBUSY;
	}

	printk("ibm405lp_setup_pcmcia:  phys addr = %lx,  virt addr = %p\n",
	       PCCF_4XX_MACRO_PADDR, *vaddr);

	*io_base = (unsigned long) ioremap(PCCF_4XX_IO_PADDR,
					   PCCF_4XX_IO_WINSIZE);
	if (*io_base == 0) {
		printk(KERN_ERR "pccf_4xx: ioremap io at 0x%lx failed.\n",
		       PCCF_4XX_IO_PADDR);
		return -EBUSY;
	}

	*mem_base = (unsigned long) ioremap(PCCF_4XX_MEM_PADDR,
					    PCCF_4XX_MEM_WINSIZE);
	if (*mem_base == 0) {
		printk(KERN_ERR "pccf_4xx: ioremap mem at 0x%lx failed.\n",
		       PCCF_4XX_MEM_PADDR);
		return -EBUSY;
	}

	return 0;
}


/****************************************************************************
 * TODC
 ****************************************************************************/

/*
 * The 405LP includes an MC146818-equivalent core accessed via a DCR
 * wrapper.  The 405LP does not implement the NVRAM.
 */

long __init ibm405lp_time_init(void)
{
	static int not_initialized = 1;

	/* Make sure clocks are running */
	if (not_initialized) {
		/* Reset the core and ensure it's enabled. */
		mtdcr(DCRN_RTC0_WRAP, 0);		/* toggle NRST & NMR */
		mtdcr(DCRN_RTC0_WRAP, 3);
		mtdcr(DCRN_RTC0_CR0, 0x60);		/* No divider chain, No square wave */
		mtdcr(DCRN_RTC0_CR1, 0x80);		/* Disable update cycles/interrupts*/
		mtdcr(DCRN_RTC0_WRAP, 0);       /* toggle NRST & NMR */
		mtdcr(DCRN_RTC0_WRAP, 3);

		/* if necessary, set the input clock frequency */
		if ((mfdcr(DCRN_RTC0_CR0) >> 4) != RTC_DVBITS) {
			printk(KERN_WARNING "Warning: RTC frequency was incorrect\n");
			mtdcr(DCRN_RTC0_CR0,
					 ((RTC_DVBITS & 0x7) << 4) | (mfdcr(DCRN_RTC0_CR0) & 0xf));
		}

		mtdcr(DCRN_RTC0_CR1, mfdcr(DCRN_RTC0_CR1) & 0x7f);	/* allow updates */

		not_initialized = 0;
	}

	return 0;
}

unsigned long ibm405lp_get_rtc_time(void)
{
	uint	year, mon, day, hour, min, sec;
	uint	i, pm;
	u_char	save_control, uip;

	spin_lock(&rtc_lock);
	save_control = mfdcr(DCRN_RTC0_CR1);

	for (i=0; i<100000000; i++) {
		uip = mfdcr(DCRN_RTC0_CR0);
		sec = mfdcr(DCRN_RTC0_SEC) & 0x7f;
		min = mfdcr(DCRN_RTC0_MIN) & 0x7f;
		hour = mfdcr(DCRN_RTC0_HR) & 0xff;
		day = mfdcr(DCRN_RTC0_DOM) & 0x3f;
		mon = mfdcr(DCRN_RTC0_MONTH) & 0x1f;
		year = mfdcr(DCRN_RTC0_YEAR) & 0xff;

		uip |= mfdcr(DCRN_RTC0_CR0);
		if ((uip & RTC_UIP) == 0) break;
	}

	spin_unlock(&rtc_lock);

	pm = hour & 0x80;
	hour = hour & 0x3f;

	if (((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BCD_TO_BIN(sec);
		BCD_TO_BIN(min);
		BCD_TO_BIN(hour);
		BCD_TO_BIN(day);
		BCD_TO_BIN(mon);
		BCD_TO_BIN(year);
	}

	
	/* 11:59 AM -> 12:00 PM ->-> 1:00 PM ->-> 11:59 PM -> 12:00 AM */
	if (!(save_control & RTC_24H)) {
		if (pm && (hour != 12))
			hour += 12;
		if (!pm && (hour == 12))
			hour = 0;
	}

	year = year + 1900;
	if (year < 1970) {
		year += 100;
	}

	return mktime(year, mon, day, hour, min, sec);
}

int ibm405lp_set_rtc_time(unsigned long nowtime)
{
	struct rtc_time	tm;
	u_char		save_control, save_freq_select, pm = 0;

	spin_lock(&rtc_lock);
	to_tm(nowtime, &tm);

	save_control = mfdcr(DCRN_RTC0_CR1);
	save_freq_select = mfdcr(DCRN_RTC0_CR0);
	mtdcr(DCRN_RTC0_CR0, save_freq_select | RTC_DIV_RESET2);

        tm.tm_year = (tm.tm_year - 1900) % 100;

	/* 11:59 AM -> 12:00 PM ->-> 1:00 PM ->-> 11:59 PM -> 12:00 AM */
	if (!(save_control & RTC_24H)) {
		if (tm.tm_hour == 0)
			tm.tm_hour = 12;
		else if (tm.tm_hour >= 12) {
			pm = 0x80;
			if (tm.tm_hour > 12) 
				tm.tm_hour -= 12;
		}
	}

	if (((save_control & RTC_DM_BINARY) == 0) ||
	    RTC_ALWAYS_BCD) {

		BIN_TO_BCD(tm.tm_sec);
		BIN_TO_BCD(tm.tm_min);
		BIN_TO_BCD(tm.tm_hour);
		BIN_TO_BCD(tm.tm_mon);
		BIN_TO_BCD(tm.tm_mday);
		BIN_TO_BCD(tm.tm_year);
	}

	tm.tm_hour |= pm;

	mtdcr(DCRN_RTC0_SEC,   tm.tm_sec);
	mtdcr(DCRN_RTC0_MIN,   tm.tm_min);
	mtdcr(DCRN_RTC0_HR,    tm.tm_hour);
	mtdcr(DCRN_RTC0_MONTH, tm.tm_mon);
	mtdcr(DCRN_RTC0_DOM,   tm.tm_mday);
	mtdcr(DCRN_RTC0_YEAR,  tm.tm_year);
	mtdcr(DCRN_RTC0_WRAP, 0); /* Reset divider chain */ 
	mtdcr(DCRN_RTC0_WRAP, 3);
	mtdcr(DCRN_RTC0_CR0, save_freq_select);

	spin_unlock(&rtc_lock);
	return 0;
}

/* ibm405lp_set_rtc_sqw()
 * Set the RTC squarewave frequency and enable SQW output. This is needed to
 * drive the APM watchdog.
 */
void ibm405lp_set_rtc_sqw(unsigned long rsbits)
{
	/* set RS field */
	mtdcr(DCRN_RTC0_CR0, (mfdcr(DCRN_RTC0_CR0) & 0xf0) | (rsbits & 0xf));

	/* set SQWE (enable squarewave output) */
	mtdcr(DCRN_RTC0_CR1, mfdcr(DCRN_RTC0_CR1) | 0x08);
}

/* The RTC only has a 24-hour alarm capability, so only the hours, minutes and
 * seconds fields of the rtc_time struct are used in alarm functions.  This
 * implementation converts all out-of-range inputs to 'don't cares' (0xff), and
 * returns 'don't cares' verbatim.  Somewhat inspired by drivers/char/rtc.c
 */

static inline int
dont_care(uint value)
{
	return (value & 0xc0) == 0xc0;
}

void ibm405lp_get_rtc_alm_time(struct rtc_time *alm_tm)
{
	uint   hour, min, sec, pm;
	u_char save_control;

	spin_lock_irq(&rtc_lock);
	sec = mfdcr(DCRN_RTC0_SECAL);
	min = mfdcr(DCRN_RTC0_MINAL);
	hour = mfdcr(DCRN_RTC0_HRAL);
	save_control = mfdcr(DCRN_RTC0_CR1);
	spin_unlock_irq(&rtc_lock);

	if (!dont_care(hour)) {
		pm = hour & 0x80;
		hour = hour & 0x3f;
	}

	if (!(save_control & RTC_DM_BINARY) || RTC_ALWAYS_BCD) {
		if (!dont_care(sec)) BCD_TO_BIN(sec);
		if (!dont_care(min)) BCD_TO_BIN(min);
		if (!dont_care(hour)) BCD_TO_BIN(hour);
	}

	/* 11:59 AM -> 12:00 PM ->-> 1:00 PM ->-> 11:59 PM -> 12:00 AM */
	if (!dont_care(hour) && !(save_control & RTC_24H)) {
		if (pm && (hour != 12))
			hour += 12;
		if (!pm && (hour == 12))
			hour = 0;
	}

	alm_tm->tm_hour = hour;
	alm_tm->tm_min = min;
	alm_tm->tm_sec = sec;
}

void ibm405lp_set_rtc_alm_time(struct rtc_time *alm_tm)
{
	uint   hour, min, sec, pm = 0;
	u_char save_control;

	hour = alm_tm->tm_hour;
	min = alm_tm->tm_min;
	sec = alm_tm->tm_sec;

	if (hour > 23) hour = 0xff;
	if (min > 59) min = 0xff;
	if (sec > 59) sec = 0xff;

	spin_lock_irq(&rtc_lock);

	save_control = mfdcr(DCRN_RTC0_CR1);

	/* 11:59 AM -> 12:00 PM ->-> 1:00 PM ->-> 11:59 PM -> 12:00 AM */
	if (!dont_care(hour) && !(save_control & RTC_24H)) {
		if (hour == 0)
			hour = 12;
		else if (hour >= 12) {
			pm = 0x80;
			if (hour > 12) 
				hour -= 12;
		}
	}

	if (((save_control & RTC_DM_BINARY) == 0) || RTC_ALWAYS_BCD) {
		if (!dont_care(sec)) BIN_TO_BCD(sec);
		if (!dont_care(min)) BIN_TO_BCD(min);
		if (!dont_care(hour)) BIN_TO_BCD(hour);
	}

	mtdcr(DCRN_RTC0_HRAL, hour | pm);
	mtdcr(DCRN_RTC0_MINAL, min);
	mtdcr(DCRN_RTC0_SECAL, sec);

	spin_unlock_irq(&rtc_lock);
}

/****************************************************************************
 * APM Unit PIC
 ****************************************************************************/

/* The 405LP "APM" unit implements a hierarchical interrupt controller.  This
   controller is buried in the APM unit, and is not part of the UIC. Three
   special "wakeup" interrupts and the RTC interrupt are handled here. These
   interrupts and this controller are special for several reasons:

   1) They are the only interrupts that can wake the system from an
   APM-controlled low-power state. 

   2) The APM DCRs actually function at the RTC frequency.  Writes to these
   registers do not take effect for several RTC cycles, and must be done with
   the 'mtdcr_interlock()' protocol as they all have 'valid' bits.

   3) The function of this unit is predictable, but difficult to understand.

   In this implementation, RTC and wakeup IRQ handlers will attach to the APM
   interrupt using the shared interrupt protocol.  The individual handlers are
   responsible for setting up their polarity and triggers and clearing the
   interrupt conditions they service, using the routines defined here. This
   seemed simpler than going into ppc4xx_pic.c and implementing a new type of
   cascaded IRQ service, esp. given that that this structure is unique to the
   405LP and has such an unusual specification. 

   Hardware Notes: Wakeup input trigger and level conditions are programmable
   just like in the UIC, and the "wakeup" interrupts can be used for any
   purpose.  Unfortunately there's no way to determine the input level of a
   "wakeup" input other than by generating interrupt status. The APM interrupt
   controller ORs the enabled status of the 4 interrupts it controls and
   presents a single, active-high, level-triggered interrupt to the UIC.  This
   signal is also used as the wakeup indication when the device is asleep.

   The function of the APM0_IER and APM0_ISR are reversed from their usage in
   the UIC.  Rather than IER masking status in ISR, status for _all_ bits in
   ISR only appears when _any_ bit in IER is set.  The interrupt signal is the
   correct reduced OR of IER & ISR, however.  Furthermore, the ISR will only
   generate new status if the final value written to it is 0 after any status
   clearing operations. 

   The RTC0_CR1 and RTC0_ISR registers will be cleared during unpowered sleep
   states (power-down, standby, hibernate). The device will still generate RTC
   alarm interrupts correctly though, so the clearing must take place on power
   up. */

void
ibm405lp_apm_dcr_delay(void)
{
  udelay(153);			/* 5 RTC cycles at 32768 Hz */
}

void
ibm405lp_apm_irq_enable(unsigned apm_irq)
{
	u32 ier = mfdcr(DCRN_APM0_IER);

	mtdcr_interlock(DCRN_APM0_IER, ier | (1 << (31 - apm_irq)), 
			APM0_IRQ_MASK);
	ibm405lp_apm_dcr_delay();
}

void
ibm405lp_apm_irq_disable(unsigned apm_irq)
{
	u32 ier = mfdcr(DCRN_APM0_IER);

	mtdcr_interlock(DCRN_APM0_IER, ier & ~(1 << (31 - apm_irq)),
			APM0_IRQ_MASK);
	ibm405lp_apm_dcr_delay();
}

void
ibm405lp_apm_irq_ack(unsigned apm_irq)
{
	mtdcr_interlock(DCRN_APM0_ISR, 1 << (31 - apm_irq), APM0_IRQ_MASK);
	ibm405lp_apm_dcr_delay();

	mtdcr(DCRN_APM0_ISR, 0);
	ibm405lp_apm_dcr_delay();
}

int
ibm405lp_apm_irq_status(unsigned apm_irq)
{
	return mfdcr(DCRN_APM0_ISR) & mfdcr(DCRN_APM0_IER) & 
		(1 << (31 - apm_irq));
}

/* Setup should be called with the APM IRQ disabled.  Since changing parameters
   may cause status to be asserted, it is always ack'ed before returning. */

void
ibm405lp_apm_irq_setup(unsigned apm_irq, unsigned trigger, unsigned polarity)
{
	u32 itr, ipr;

	itr = mfdcr(DCRN_APM0_ITR) | ((trigger ? 1 : 0) << (31 - apm_irq));
	mtdcr_interlock(DCRN_APM0_ITR, itr, APM0_IRQ_MASK);
	ipr = mfdcr(DCRN_APM0_IPR) | ((polarity ? 1 : 0) << (31 - apm_irq));
	mtdcr_interlock(DCRN_APM0_IPR, ipr, APM0_IRQ_MASK);
	ibm405lp_apm_dcr_delay();
	ibm405lp_apm_irq_ack(apm_irq);
}

/* The RTC interrupt is always level-triggered, active high.  Others are board
   dependent. */

void __init
ibm405lp_setup_apm_pic(void)
{
	mtdcr_interlock(DCRN_APM0_IER, 0, APM0_IRQ_MASK);
	ibm405lp_apm_dcr_delay();
	ibm405lp_apm_irq_setup(APM0_IRQ_RTC, 0, 1);
}


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
