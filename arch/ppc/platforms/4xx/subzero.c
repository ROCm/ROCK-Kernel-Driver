/*
 * arch/ppc/platforms/subzero.c  Platform setup for the IBM Subzero CPU core card.
 *
 *				Based on arctic1.c by Ken Inoue, which
 *				was based on beech.c by Bishop Brock
 *
 * The source code contained herein is licensed under the IBM Public License
 * Version 1.0, which has been approved by the Open Source Initiative.
 * Copyright (C) 2002, International Business Machines Corporation
 * All Rights Reserved.
 *
 * David Gibson
 * IBM OzLabs, Canberra, Australia
 * <dwg@au1.ibm.com>
 */

#include <linux/blk.h>
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

/* 
   Subzero core card physical memory map:

   Main Memory (Initialized by the BIOS)
   =======================================================================

   SDRAM (32 MB)     0x00000000 - 0x02000000

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

   
   EBC Space: (Mapped virtual = physical in board_io_mapping())
	      (EBC setup for personality cards left to individual card setups) 
   Space             EBC Bank    Physical Addresses  EBC Base Address
   =========================================================================
   Boot/Linux Flash      0       FF000000 - FFFFFFFF  FF000000 (16MB)

*/


/****************************************************************************
 * EBC Setup
 ****************************************************************************/

/* The EBC is set up for Arctic1.  This may simply replicate the setup already
   done by the IBM BIOS for Arctic1 (possibly with some address map changes), or
   may be the first initialization if the board is booting from another BIOS.
   Virtually all that is required to boot Linux on Subzero is that the BIOS
   enable the memory controller, load a Linux image from flash, and run it.

   For optimal dynamic frequency scaling the EBC settings will also vary as the
   frequency varies.
*/

void __init
subzero_core_ebc_setup(void)
{
	ebc0_bnap_t ap;

	/* Set EBC bank 0 for the boot/data flash.

	   Access parameters assume 150ns Intel flash @ 66.66 MHz maximum bus
	   speed = 10 cycle access with 2 turnaround cycles (30 ns).

	   NB: IBM BIOS sets this bank to burst, however bursting will never
	   happen in Linux because this region is mapped non-cacheable and
	   guarded, so it is set non-burst here. */
	ap.reg = mfdcri(DCRN_EBC0, BnAP(0)) & EBC0_BnAP_MASK;
	ap.fields.twt = 10;
	ap.fields.th = 2;
	mtdcri(DCRN_EBC0, BnAP(0), ap.reg);

}

