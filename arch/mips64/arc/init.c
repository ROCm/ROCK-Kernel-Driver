/* $Id: init.c,v 1.3 1999/11/19 23:29:05 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM library initialisation code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/sgialib.h>

#undef DEBUG_PROM_INIT

/* Master romvec interface. */
struct linux_romvec *romvec;
PSYSTEM_PARAMETER_BLOCK sgi_pblock;
int prom_argc;
LONG *_prom_argv, *_prom_envp;
unsigned short prom_vers, prom_rev;

extern void prom_testtree(void);

int __init
prom_init(int argc, char **argv, char **envp)
{
	PSYSTEM_PARAMETER_BLOCK pb;

	romvec = ROMVECTOR;
	pb = sgi_pblock = PROMBLOCK;
	prom_argc = argc;
	_prom_argv = (LONG *) argv;
	_prom_envp = (LONG *) envp;

	if(pb->magic != 0x53435241) {
		prom_printf("Aieee, bad prom vector magic %08lx\n", pb->magic);
		while(1)
			;
	}

	prom_init_cmdline();

	prom_vers = pb->ver;
	prom_rev = pb->rev;
	prom_identify_arch();
	printk("PROMLIB: ARC firmware Version %d Revision %d\n",
		    prom_vers, prom_rev);
	prom_meminit();

#ifdef DEBUG_PROM_INIT
	{
		prom_printf("Press a key to reboot\n");
		(void) prom_getchar();
		ArcEnterInteractiveMode();
	}
#endif
	return 0;
}
