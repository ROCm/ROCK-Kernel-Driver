/*
 * identify.c: machine identification code.
 *
 * Copyright (C) 1998 Harald Koerfgen and Paul M. Antoine
 *
 * $Id: identify.c,v 1.2 1999/10/09 00:00:58 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/bootinfo.h>

#include "dectypes.h"
#include "prom.h"

extern char *(*prom_getenv)(char *);
extern int (*prom_printf)(char *, ...);
extern int (*rex_getsysid)(void);

extern unsigned long mips_machgroup;
extern unsigned long mips_machtype;

void __init prom_identify_arch (unsigned int magic)
{
	unsigned char dec_cpunum, dec_firmrev, dec_etc;
	int dec_systype;
	unsigned long dec_sysid;

	if (magic != REX_PROM_MAGIC) {
		dec_sysid = simple_strtoul(prom_getenv("systype"), (char **)0, 0);
	} else {
		dec_sysid = rex_getsysid();
		if (dec_sysid == 0) {
			prom_printf("Zero sysid returned from PROMs! Assuming PMAX-like machine.\n");
			dec_sysid = 1;
		}
	}

	dec_cpunum = (dec_sysid & 0xff000000) >> 24;
	dec_systype = (dec_sysid & 0xff0000) >> 16;
	dec_firmrev = (dec_sysid & 0xff00) >> 8;
	dec_etc = dec_sysid & 0xff;

	/* We're obviously one of the DEC machines */
	mips_machgroup = MACH_GROUP_DEC;

	/*
	 * FIXME: This may not be an exhaustive list of DECStations/Servers!
	 * Put all model-specific initialisation calls here.
	 */
	prom_printf("This DECstation is a ");

	switch (dec_systype) {
	case DS2100_3100:
		prom_printf("DS2100/3100\n");
		mips_machtype = MACH_DS23100;
		break;
	case DS5100:		/* DS5100 MIPSMATE */
		prom_printf("DS5100\n");
		mips_machtype = MACH_DS5100;
		break;
	case DS5000_200:	/* DS5000 3max */
		prom_printf("DS5000/200\n");
		mips_machtype = MACH_DS5000_200;
		break;
	case DS5000_1XX:	/* DS5000/100 3min */
		prom_printf("DS5000/1xx\n");
		mips_machtype = MACH_DS5000_1XX;
		break;
	case DS5000_2X0:	/* DS5000/240 3max+ */
		prom_printf("DS5000/2x0\n");
		mips_machtype = MACH_DS5000_2X0;
		break;
	case DS5000_XX:	/* Personal DS5000/2x */
		prom_printf("Personal DS5000/xx\n");
		mips_machtype = MACH_DS5000_XX;
		break;
	case DS5800:		/* DS5800 Isis */
		prom_printf("DS5800\n");
		mips_machtype = MACH_DS5800;
		break;
	case DS5400:		/* DS5400 MIPSfair */
		prom_printf("DS5400\n");
		mips_machtype = MACH_DS5400;
		break;
	case DS5500:		/* DS5500 MIPSfair-2 */
		prom_printf("DS5500\n");
		mips_machtype = MACH_DS5500;
		break;
	default:
		prom_printf("unknown, id is %x", dec_systype);
		mips_machtype = MACH_DSUNKNOWN;
		break;
	}
}


