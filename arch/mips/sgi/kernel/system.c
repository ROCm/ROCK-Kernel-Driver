/*
 * system.c: Probe the system type using ARCS prom interface library.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: system.c,v 1.8 1999/10/09 00:00:59 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/sgi/sgi.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>

enum sgi_mach sgimach;

struct smatch {
	char *name;
	int type;
};

static struct smatch sgi_cputable[] = {
	{ "MIPS-R2000", CPU_R2000 },
	{ "MIPS-R3000", CPU_R3000 },
	{ "MIPS-R3000A", CPU_R3000A },
	{ "MIPS-R4000", CPU_R4000SC },
	{ "MIPS-R4400", CPU_R4400SC },
	{ "MIPS-R4600", CPU_R4600 },
	{ "MIPS-R8000", CPU_R8000 },
	{ "MIPS-R5000", CPU_R5000 },
	{ "MIPS-R5000A", CPU_R5000A }
};

#define NUM_CPUS 9 /* for now */

static int __init string_to_cpu(char *s)
{
	int i;

	for(i = 0; i < NUM_CPUS; i++) {
		if(!strcmp(s, sgi_cputable[i].name))
			return sgi_cputable[i].type;
	}
	prom_printf("\nYeee, could not determine MIPS cpu type <%s>\n", s);
	prom_printf("press a key to reboot\n");
	prom_getchar();
	romvec->imode();
	return 0;
}

/*
 * We' call this early before loadmmu().  If we do the other way around
 * the firmware will crash and burn.
 */
void __init sgi_sysinit(void)
{
	pcomponent *p, *toplev, *cpup = 0;
	int cputype = -1;


	/* The root component tells us what machine architecture we
	 * have here.
	 */
	p = prom_getchild(PROM_NULL_COMPONENT);

	/* Now scan for cpu(s). */
	toplev = p = prom_getchild(p);
	while(p) {
		int ncpus = 0;

		if(p->type == Cpu) {
			if(++ncpus > 1) {
				prom_printf("\nYeee, SGI MP not ready yet\n");
				prom_printf("press a key to reboot\n");
				prom_getchar();
				romvec->imode();
			}
			printk("CPU: %s ", p->iname);
			cpup = p;
			cputype = string_to_cpu(cpup->iname);
		}
		p = prom_getsibling(p);
	}
	if(cputype == -1) {
		prom_printf("\nYeee, could not find cpu ARCS component\n");
		prom_printf("press a key to reboot\n");
		prom_getchar();
		romvec->imode();
	}
	p = prom_getchild(cpup);
	while(p) {
		switch(p->class) {
		case processor:
			switch(p->type) {
			case Fpu:
				printk("FPU<%s> ", p->iname);
				break;

			default:
				break;
			};
			break;

		case cache:
			switch(p->type) {
			case picache:
				printk("ICACHE ");
				break;

			case pdcache:
				printk("DCACHE ");
				break;

			case sccache:
				printk("SCACHE ");
				break;

			default:
				break;

			};
			break;

		default:
			break;
		};
		p = prom_getsibling(p);
	}
	printk("\n");
}
