/* $Id: cf-enabler.c,v 1.2 2000/06/08 05:50:10 gniibe Exp $
 *
 *  linux/drivers/block/cf-enabler.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2000  Toshiharu Nozawa
 *
 *  Enable the CF configuration.
 */

#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/hitachi_se.h>


/*
 * SolutionEngine
 *
 * 0xB8400000 : Common Memory
 * 0xB8500000 : Attribute
 * 0xB8600000 : I/O
 */

static int __init cf_init_se(void)
{
	if ((ctrl_inw(MRSHPC_CSR) & 0x000c) != 0)
		return 0;	/* Not detected */

	if ((ctrl_inw(MRSHPC_CSR) & 0x0080) == 0) {
		ctrl_outw(0x0674, MRSHPC_CPWCR); /* Card Vcc is 3.3v? */
	} else {
		ctrl_outw(0x0678, MRSHPC_CPWCR); /* Card Vcc is 5V */
	}

	/*
	 *  PC-Card window open 
	 *  flag == COMMON/ATTRIBUTE/IO
	 */
	/* common window open */
	ctrl_outw(0x8a84, MRSHPC_MW0CR1);/* window 0xb8400000 */
	if((ctrl_inw(MRSHPC_CSR) & 0x4000) != 0)
		/* common mode & bus width 16bit SWAP = 1*/
		ctrl_outw(0x0b00, MRSHPC_MW0CR2);
	else
		/* common mode & bus width 16bit SWAP = 0*/
		ctrl_outw(0x0300, MRSHPC_MW0CR2); 

	/* attribute window open */
	ctrl_outw(0x8a85, MRSHPC_MW1CR1);/* window 0xb8500000 */
	if ((ctrl_inw(MRSHPC_CSR) & 0x4000) != 0)
		/* attribute mode & bus width 16bit SWAP = 1*/
		ctrl_outw(0x0a00, MRSHPC_MW1CR2);
	else
		/* attribute mode & bus width 16bit SWAP = 0*/
		ctrl_outw(0x0200, MRSHPC_MW1CR2);

	/* I/O window open */
	ctrl_outw(0x8a86, MRSHPC_IOWCR1);/* I/O window 0xb8600000 */
	ctrl_outw(0x0008, MRSHPC_CDCR);	 /* I/O card mode */
	if ((ctrl_inw(MRSHPC_CSR) & 0x4000) != 0)
		ctrl_outw(0x0a00, MRSHPC_IOWCR2); /* bus width 16bit SWAP = 1*/
	else
		ctrl_outw(0x0200, MRSHPC_IOWCR2); /* bus width 16bit SWAP = 0*/

	ctrl_outw(0x2000, MRSHPC_ICR);
	ctrl_outb(0x00, PA_MRSHPC_MW2 + 0x206);
	ctrl_outb(0x42, PA_MRSHPC_MW2 + 0x200);
	return 0;
}

#define CF_CIS_BASE	0xb8000000
/*
 * You can connect Compact Flash directly to the bus of SuperH.
 * This is the enabler for that.
 *
 * SIM: How generic is this really? It looks pretty board, or at
 * least SH sub-type, specific to me.
 * I know it doesn't work on the Overdrive!
 */

/*
 * 0xB8000000 : Attribute
 * 0xB8001000 : Common Memory
 * 0xBA000000 : I/O
 */

static int __init cf_init_default(void)
{
	/* Enable the card, and set the level interrupt */
	ctrl_outw(0x0042, CF_CIS_BASE+0x0200);
	make_imask_irq(14);
	disable_irq(14);
	return 0;
}

int __init cf_init(void)
{
	if (MACH_SE) {
		return cf_init_se();
	}
	return cf_init_default();
}

__initcall (cf_init);
