/*
 * This file is subject to the terms and conditions of the GNU General Public+ 
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
struct linux_promblock *sgi_pblock;
int prom_argc;
char **prom_argv, **prom_envp;
unsigned short prom_vers, prom_rev;

extern void prom_testtree(void);

extern void arc_setup_console(void);

void __init prom_init(int argc, char **argv, char **envp, int *prom_vec)
{
	struct linux_promblock *pb;

	romvec = ROMVECTOR;
	pb = sgi_pblock = PROMBLOCK;
	prom_argc = argc;
	prom_argv = argv;
	prom_envp = envp;

#if 0
	/* arc_printf should not use prom_printf as soon as we free
	 * the prom buffers - This horribly breaks on Indys with framebuffer
	 * as it simply stops after initialising swap - On the Indigo2 serial
	 * console you will get A LOT illegal instructions - Only enable
	 * this for early init crashes - This also brings up artefacts of
	 * printing everything twice on serial console and on GFX Console
	 * this has the effect of having the prom printing everything
	 * in the small rectangle and the kernel printing around.
	 */

	arc_setup_console();
#endif
	if (pb->magic != 0x53435241) {
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
		romvec->imode();
	}
#endif
}
