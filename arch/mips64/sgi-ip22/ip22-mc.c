/* $Id: ip22-mc.c,v 1.2 1999/10/19 20:51:52 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * indy_mc.c: Routines for manipulating the INDY memory controller.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/addrspace.h>
#include <asm/ptrace.h>
#include <asm/sgi/sgimc.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgialib.h>

/* #define DEBUG_SGIMC */

struct sgimc_misc_ctrl *mcmisc_regs;
u32 *rpsscounter;
struct sgimc_dma_ctrl *dmactrlregs;

static inline char *mconfig_string(unsigned long val)
{
	switch(val & SGIMC_MCONFIG_RMASK) {
	case SGIMC_MCONFIG_FOURMB:
		return "4MB";

	case SGIMC_MCONFIG_EIGHTMB:
		return "8MB";

	case SGIMC_MCONFIG_SXTEENMB:
		return "16MB";

	case SGIMC_MCONFIG_TTWOMB:
		return "32MB";

	case SGIMC_MCONFIG_SFOURMB:
		return "64MB";

	case SGIMC_MCONFIG_OTEIGHTMB:
		return "128MB";

	default:
		return "wheee, unknown";
	};
}

void __init sgimc_init(void)
{
	unsigned long tmpreg;

	mcmisc_regs = (struct sgimc_misc_ctrl *)(KSEG1+0x1fa00000);
	rpsscounter = (u32 *) (KSEG1 + 0x1fa01004);
	dmactrlregs = (struct sgimc_dma_ctrl *) (KSEG1+0x1fa02000);

	printk("MC: SGI memory controller Revision %d\n",
	       (int) mcmisc_regs->systemid & SGIMC_SYSID_MASKREV);

#if 0 /* XXX Until I figure out what this bit really indicates XXX */
	/* XXX Is this systemid bit reliable? */
	if(mcmisc_regs->systemid & SGIMC_SYSID_EPRESENT) {
		EISA_bus = 1;
		printk("with EISA\n");
	} else {
		EISA_bus = 0;
		printk("no EISA\n");
	}
#endif

#ifdef DEBUG_SGIMC
	prom_printf("sgimc_init: memconfig0<%s> mconfig1<%s>\n",
		    mconfig_string(mcmisc_regs->mconfig0),
		    mconfig_string(mcmisc_regs->mconfig1));

	prom_printf("mcdump: cpuctrl0<%08lx> cpuctrl1<%08lx>\n",
		    mcmisc_regs->cpuctrl0, mcmisc_regs->cpuctrl1);
	prom_printf("mcdump: divider<%08lx>, gioparm<%04x>\n",
		    mcmisc_regs->divider, mcmisc_regs->gioparm);
#endif

	/* Place the MC into a known state.  This must be done before
	 * interrupts are first enabled etc.
	 */

	/* Step 1: The CPU/GIO error status registers will not latch
	 *         up a new error status until the register has been
	 *         cleared by the cpu.  These status registers are
	 *         cleared by writing any value to them.
	 */
	mcmisc_regs->cstat = mcmisc_regs->gstat = 0;

	/* Step 2: Enable all parity checking in cpu control register
	 *         zero.
	 */
	tmpreg = mcmisc_regs->cpuctrl0;
	tmpreg |= (SGIMC_CCTRL0_EPERRGIO | SGIMC_CCTRL0_EPERRMEM |
		   SGIMC_CCTRL0_R4KNOCHKPARR);
	mcmisc_regs->cpuctrl0 = tmpreg;

	/* Step 3: Setup the MC write buffer depth, this is controlled
	 *         in cpu control register 1 in the lower 4 bits.
	 */
	tmpreg = mcmisc_regs->cpuctrl1;
	tmpreg &= ~0xf;
	tmpreg |= 0xd;
	mcmisc_regs->cpuctrl1 = tmpreg;

	/* Step 4: Initialize the RPSS divider register to run as fast
	 *         as it can correctly operate.  The register is laid
	 *         out as follows:
	 *
	 *         ----------------------------------------
	 *         |  RESERVED  |   INCREMENT   | DIVIDER |
	 *         ----------------------------------------
	 *          31        16 15            8 7       0
	 *
	 *         DIVIDER determines how often a 'tick' happens,
	 *         INCREMENT determines by how the RPSS increment
	 *         registers value increases at each 'tick'. Thus,
	 *         for IP22 we get INCREMENT=1, DIVIDER=1 == 0x101
	 */
	mcmisc_regs->divider = 0x101;

	/* Step 5: Initialize GIO64 arbitrator configuration register.
	 *
	 * NOTE: If you dork with startup code the HPC init code in
	 *       sgihpc_init() must run before us because of how we
	 *       need to know Guiness vs. FullHouse and the board
	 *       revision on this machine.  You have been warned.
	 */

	/* First the basic invariants across all gio64 implementations. */
	tmpreg = SGIMC_GIOPARM_HPC64;    /* All 1st HPC's interface at 64bits. */
	tmpreg |= SGIMC_GIOPARM_ONEBUS;  /* Only one physical GIO bus exists. */

	if(sgi_guiness) {
		/* Guiness specific settings. */
		tmpreg |= SGIMC_GIOPARM_EISA64;     /* MC talks to EISA at 64bits */
		tmpreg |= SGIMC_GIOPARM_MASTEREISA; /* EISA bus can act as master */
	} else {
		/* Fullhouse specific settings. */
		if(sgi_boardid < 2) {
			tmpreg |= SGIMC_GIOPARM_HPC264; /* 2nd HPC at 64bits */
			tmpreg |= SGIMC_GIOPARM_PLINEEXP0; /* exp0 pipelines */
			tmpreg |= SGIMC_GIOPARM_MASTEREXP1;/* exp1 masters */
			tmpreg |= SGIMC_GIOPARM_RTIMEEXP0; /* exp0 is realtime */
		} else {
			tmpreg |= SGIMC_GIOPARM_HPC264; /* 2nd HPC 64bits */
			tmpreg |= SGIMC_GIOPARM_PLINEEXP0; /* exp[01] pipelined */
			tmpreg |= SGIMC_GIOPARM_PLINEEXP1;
			tmpreg |= SGIMC_GIOPARM_MASTEREISA;/* EISA masters */
			/* someone forgot this poor little guy... */
			tmpreg |= SGIMC_GIOPARM_GFX64; 	/* GFX at 64 bits */
		}
	}
	mcmisc_regs->gioparm = tmpreg; /* poof */
}
