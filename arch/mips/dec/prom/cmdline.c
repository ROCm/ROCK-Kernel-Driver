/*
 * cmdline.c: read the command line passed to us by the PROM.
 *
 * Copyright (C) 1998 Harald Koerfgen
 *
 * $Id: cmdline.c,v 1.2 1999/10/09 00:00:57 ralf Exp $
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/bootinfo.h>

#include "prom.h"

#undef PROM_DEBUG

#ifdef PROM_DEBUG
extern int (*prom_printf)(char *, ...);
#endif

char arcs_cmdline[CL_SIZE];

void __init prom_init_cmdline(int argc, char **argv, unsigned long magic)
{
	int start_arg, i;

	/*
	 * collect args and prepare cmd_line
	 */
	if (magic != REX_PROM_MAGIC)
		start_arg = 1;
	else
		start_arg = 2;
	for (i = start_arg; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}

#ifdef PROM_DEBUG
	prom_printf("arcs_cmdline: %s\n", &(arcs_cmdline[0]));
#endif

}

