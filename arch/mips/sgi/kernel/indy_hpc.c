/* $Id: indy_hpc.c,v 1.9 1999/12/04 03:59:00 ralf Exp $
 *
 * indy_hpc.c: Routines for generic manipulation of the HPC controllers.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1998 Ralf Baechle
 */
#include <linux/init.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>
#include <asm/sgialib.h>

/* #define DEBUG_SGIHPC */

struct hpc3_regs *hpc3c0, *hpc3c1;
struct hpc3_miscregs *hpc3mregs;

/* We need software copies of these because they are write only. */
unsigned int sgi_hpc_write1, sgi_hpc_write2;

/* Machine specific identifier knobs. */
int sgi_has_ioc2 = 0;
int sgi_guiness = 0;
int sgi_boardid;

void __init sgihpc_init(void)
{
	unsigned long sid, crev, brev;

	hpc3c0 = (struct hpc3_regs *) (KSEG1 + HPC3_CHIP0_PBASE);
	hpc3c1 = (struct hpc3_regs *) (KSEG1 + HPC3_CHIP1_PBASE);
	hpc3mregs = (struct hpc3_miscregs *) (KSEG1 + HPC3_MREGS_PBASE);
	sid = hpc3mregs->sysid;

	sid &= 0xff;
	crev = (sid & 0xe0) >> 5;
	brev = (sid & 0x1e) >> 1;

#ifdef DEBUG_SGIHPC
	prom_printf("sgihpc_init: crev<%2x> brev<%2x>\n", crev, brev);
	prom_printf("sgihpc_init: ");
#endif

	/* This test works now thanks to William J. Earl */
	if ((sid & 1) == 0 ) {
#ifdef DEBUG_SGIHPC
		prom_printf("GUINESS ");
#endif
		sgi_guiness = 1;
	} else {
#ifdef DEBUG_SGIHPC
		prom_printf("FULLHOUSE ");
#endif
		sgi_guiness = 0;
	}
	sgi_boardid = brev;

#ifdef DEBUG_SGIHPC
	prom_printf("sgi_boardid<%d> ", sgi_boardid);
#endif

	if(crev == 1) {
		if((sid & 1) || (brev >= 2)) {
#ifdef DEBUG_SGIHPC
			prom_printf("IOC2 ");
#endif
			sgi_has_ioc2 = 1;
		} else {
#ifdef DEBUG_SGIHPC
			prom_printf("IOC1 revision 1 ");
#endif
		}
	} else {
#ifdef DEBUG_SGIHPC
		prom_printf("IOC1 revision 0 ");
#endif
	}
#ifdef DEBUG_SGIHPC
	prom_printf("\n");
#endif

	sgi_hpc_write1 = (HPC3_WRITE1_PRESET |
		  HPC3_WRITE1_KMRESET |
		  HPC3_WRITE1_ERESET |
		  HPC3_WRITE1_LC0OFF);

	sgi_hpc_write2 = (HPC3_WRITE2_EASEL |
		  HPC3_WRITE2_NTHRESH |
		  HPC3_WRITE2_TPSPEED |
		  HPC3_WRITE2_EPSEL |
		  HPC3_WRITE2_U0AMODE |
		  HPC3_WRITE2_U1AMODE);

	if(!sgi_guiness)
		sgi_hpc_write1 |= HPC3_WRITE1_GRESET;
	hpc3mregs->write1 = sgi_hpc_write1;
	hpc3mregs->write2 = sgi_hpc_write2;

	hpc3c0->pbus_piocfgs[0][6] |= HPC3_PIOPCFG_HW;
}
