/*
 * cmdline.c: Kernel command line creation using ARCS argc/argv.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>

#undef DEBUG_CMDLINE

char arcs_cmdline[COMMAND_LINE_SIZE];

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
	"OSLoadFilename=",
	"OSLoadOptions="
};
#define NENTS(foo) ((sizeof((foo)) / (sizeof((foo[0])))))

static char *used_arc[][2] = {
	{ "OSLoadPartition=", "root=" },
	{ "OSLoadOptions=", "" }
};

static char * __init move_firmware_args(char* cp)
{
	char *s;
	int actr, i;

	actr = 1; /* Always ignore argv[0] */

	while (actr < prom_argc) {
		for(i = 0; i < NENTS(used_arc); i++) {
			int len = strlen(used_arc[i][0]);

			if(!strncmp(prom_argv[actr], used_arc[i][0], len)) {
			/* Ok, we want it. First append the replacement... */
				strcat(cp, used_arc[i][1]);
				cp += strlen(used_arc[i][1]);
				/* ... and now the argument */
				s = strstr(prom_argv[actr], "=");
				if(s) {
					s++;
					strcpy(cp, s);
					cp += strlen(s);
				}
				*cp++ = ' ';
				break;
			}
		}
		actr++;
	}

	return cp;
}


void __init prom_init_cmdline(void)
{
	char *cp;
	int actr, i;

	actr = 1; /* Always ignore argv[0] */

	cp = &(arcs_cmdline[0]);
	/* 
	 * Move ARC variables to the beginning to make sure they can be
	 * overridden by later arguments.
	 */
	cp = move_firmware_args(cp);

	while (actr < prom_argc) {
		for (i = 0; i < NENTS(ignored); i++) {
			int len = strlen(ignored[i]);

			if(!strncmp(prom_argv[actr], ignored[i], len))
				goto pic_cont;
		}
		/* Ok, we want it. */
		strcpy(cp, prom_argv[actr]);
		cp += strlen(prom_argv[actr]);
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
