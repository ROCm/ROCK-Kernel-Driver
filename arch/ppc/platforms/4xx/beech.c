/*
 * arch/ppc/platforms/beech.c  Platform setup for the IBM Beech board
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
 * All Rights Reserved
 *
 * Bishop Brock
 * IBM Research, Austin Center for Low-Power Computing
 * bcbrock@us.ibm.com
 * March, 2002
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/rtc.h>
#include <linux/string.h>

#include <asm/delay.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/todc.h>

static void beech_ebc_setup(void);
static void beech_fpga_setup(void);

/*
   Beech board physical memory map:

   Main Memory (Initialized by the BIOS)
   =======================================================================

   SDRAM (64 MB)     0x00000000 - 0x04000000

   OPB Space: (Mapped virtual = physical in ppc4xx_setup.c)
   =======================================================================

   UART0               	      0xEF600300
   UART1               	      0xEF600400
   IIC                 	      0xEF600500
   OPB Arbiter         	      0xEF600600
   GPIO Controller     	      0xEF600700
   CODEC Interface            0xEF600900
   Touch Panel Controller     0xEF600A00
   DES Controller             0xEF600B00


   EBC Space: (Mapped virtual = physical in ppc4xx_map_io(); EBC setup
               for PCMCIA left to 4xx_pccf)
   Space             EBC Bank    Physical Addresses  EBC Base Address
   =========================================================================

   PCMCIA (32 MB)        x       F0000000 - F1FFFFFF  F0000000

   Expansion             2       F8000000 - F8FFFFFF  F8000000
   Linux Flash (16 MB)           F9000000 - F9FFFFFF

   NVRAM (32 KB)         1       FFE00000 - FFE07FFF  FFE00000


   Ethernet(I/O)	 1	 FFE20300 - FFE2030F  FFE00000
           (MEM)	 	 FFE40000 - FFE40FFF

   FPGA_REG_4            1       FFE60000 - FFE60000  FFE00000
   FPGA_REG_0            1       FFE80000 - FFE80000  FFE00000
   FPGA_REG_1            1       FFEA0000 - FFEA0000  FFE00000
   FPGA_REG_2            1       FFEC0000 - FFEC0000  FFE00000
   FPGA_REG_3            1       FFEE0000 - FFEE0000  FFE00000

   SRAM (512 KB)         0       FFF00000 - FFF7FFFF  FFF00000

   Boot Flash  (512 KB)  0       FFF80000 - FFFFFFFF  FFF00000

   ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

   NB: On Beech 1, address ranges for Bank 2 were reversed

*/

void __init
beech_setup_arch(void)
{
	ppc4xx_setup_arch();

	TODC_INIT(TODC_TYPE_DCR146818, NULL, NULL, NULL, 8);

	/* Set up Beech FPGA. */

	beech_fpga_setup();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = beech_setup_arch;

#ifdef CONFIG_PPC_RTC
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_dcr146818_read_val;
	ppc_md.nvram_write_val = todc_dcr146818_write_val;
#endif
	/* Disable the LCD controller, which may have been left on by the
	   BIOS.  Then do initialization of the EBC. */

	mtdcri(DCRN_LCD0, DER, 0);
	beech_ebc_setup();
}

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 + Non-standard board support follows
 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/****************************************************************************
 * EBC Setup
 ****************************************************************************/

/* The EBC is set up for Beech.  This may simply replicate the setup already
   done by the IBM BIOS for Beech (possibly with some address map changes), or
   may be the first initialization if the board is booting from another BIOS.
   Virtually all that is required to boot Linux on Beech is that the BIOS
   enable the memory controller, load a Linux image from flash, and run it.

   For optimal dynamic frequency scaling the EBC settings will also vary as the
   frequency varies.
*/

static void __init
beech_ebc_setup(void)
{
	ebc0_bnap_t ap;
	ebc0_bncr_t cr;

	/* Set EBC bank 0 for the SRAM and boot flash.

	   Access parameters assume 120ns AMD flash @ 66.66 MHz maximum bus
	   speed = 8 cycle access with 2 turnaround cycles (30 ns).

	   These parameters will work for the SRAM as well, which is a 70 ns
	   part.

	   NB: IBM BIOS sets this bank to burst, however bursting will never
	   happen in Linux because this region is mapped non-cacheable and
	   guarded, so it is set non-burst here. */

	cr.reg = (BEECH_BANK0_PADDR & 0xfff00000) |
	    (mfdcri(DCRN_EBC0, BnCR(0)) & EBC0_BnCR_MASK);
	cr.fields.bs = BEECH_BANK0_EBC_SIZE;
	cr.fields.bu = EBC0_BnCR_BU_RW;
	cr.fields.bw = EBC0_BnCR_BW_16;
	mtdcri(DCRN_EBC0, BnCR(0), cr.reg);

	ap.reg = mfdcri(DCRN_EBC0, BnAP(0)) & EBC0_BnAP_MASK;
	ap.fields.twt = 8;
	ap.fields.th = 2;
	mtdcri(DCRN_EBC0, BnAP(0), ap.reg);

	/*  EBC bank 1 is used for many purposes: NVRAM, Ethernet, and FPGA
	   registers. This is a 1 MB, 16-bit bank. The access parameters must
	   handle the worst case of all of the devices.

	   The Ethernet chip needs 20 ns setup of the addresses to the I/O
	   write signal (generated from the chip select), a minimum 150 ns
	   cycle, and 30 ns of turnaround.  These settings will work for the
	   other devices as well.
	 */

	cr.reg = (BEECH_BANK1_PADDR & 0xfff00000) |
	    (mfdcri(DCRN_EBC0, BnCR(1)) & EBC0_BnCR_MASK);
	cr.fields.bs = BEECH_BANK1_EBC_SIZE;
	cr.fields.bu = EBC0_BnCR_BU_RW;
	cr.fields.bw = EBC0_BnCR_BW_16;
	mtdcri(DCRN_EBC0, BnCR(1), cr.reg);

	ap.reg = mfdcri(DCRN_EBC0, BnAP(1)) & EBC0_BnAP_MASK;
	ap.fields.twt = 10;
	ap.fields.csn = 2;
	ap.fields.th = 2;
	mtdcri(DCRN_EBC0, BnAP(1), ap.reg);

	/* Set EBC bank 2 for the big (Linux) flash.  There is 16 MB of flash,
	   but the CPLD decodes a 32 MB region.

	   Access parameters assume 90ns AMD flash @ 66.66 MHz maximum bus
	   speed = 6 cycle access with 2 turnaround cycles (30 ns).

	   NB: IBM BIOS sets this bank to burst, however bursting will never
	   happen in Linux because this region is mapped non-cacheable and
	   guarded, so it is set non-burst here. */

	cr.reg = (BEECH_BANK2_PADDR & 0xfff00000) |
	    (mfdcri(DCRN_EBC0, BnCR(2)) & EBC0_BnCR_MASK);
	cr.fields.bs = BEECH_BANK2_EBC_SIZE;
	cr.fields.bu = EBC0_BnCR_BU_RW;
	cr.fields.bw = EBC0_BnCR_BW_8;
	mtdcri(DCRN_EBC0, BnCR(2), cr.reg);

	ap.reg = mfdcri(DCRN_EBC0, BnAP(2)) & EBC0_BnAP_MASK;
	ap.fields.twt = 6;
	ap.fields.th = 2;
	mtdcri(DCRN_EBC0, BnAP(2), ap.reg);
}

/****************************************************************************
 * FPGA Setup
 ****************************************************************************/

/* The Beech FPGA is set up for Linux. */

static void __init
beech_fpga_setup(void)
{
	volatile u8 *fpga_reg_2;

	fpga_reg_2 = (volatile u8 *)
	    ioremap(BEECH_FPGA_REG_2_PADDR, BEECH_FPGA_REG_2_SIZE);

	/* Set RTS/CTS mode for UART 1 */

	*fpga_reg_2 |= FPGA_REG_2_DEFAULT_UART1_N;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
