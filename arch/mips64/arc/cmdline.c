/* $Id: cmdline.c,v 1.2 1999/10/19 20:51:44 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * cmdline.c: Kernel command line creation using ARCS argc/argv.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>

/* #define DEBUG_CMDLINE */

char arcs_cmdline[CL_SIZE];

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

static char *ignored[] = {
	"ConsoleIn=",
	"ConsoleOut=",
	"SystemPartition=",
	"OSLoader=",
	"OSLoadPartition=",
	"OSLoadFilename="
};
#define NENTS(foo) ((sizeof((foo)) / (sizeof((foo[0])))))

void __init prom_init_cmdline(void)
{
	char *cp;
	int actr, i;

	actr = 1; /* Always ignore argv[0] */

	cp = &(arcs_cmdline[0]);
	while(actr < prom_argc) {
		for(i = 0; i < NENTS(ignored); i++) {
			int len = strlen(ignored[i]);

			if(!strncmp(prom_argv(actr), ignored[i], len))
				goto pic_cont;
		}
		/* Ok, we want it. */
		strcpy(cp, prom_argv(actr));
		cp += strlen(prom_argv(actr));
		*cp++ = ' ';

	pic_cont:
		actr++;
	}
	if (cp != &(arcs_cmdline[0])) /* get rid of trailing space */
		--cp;
	*cp = '\0';

#ifdef DEBUG_CMDLINE
	prom_printf("prom_init_cmdline: %s\n", &(arcs_cmdline[0]));
#endif
}
