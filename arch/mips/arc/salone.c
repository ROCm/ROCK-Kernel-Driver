/*
 * salone.c: Routines to load into memory and execute stand-along
 *           program images using ARCS PROM firmware.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * $Id: salone.c,v 1.1 1998/10/18 13:32:09 tsbogend Exp $
 */
#include <linux/init.h>
#include <asm/sgialib.h>

long __init prom_load(char *name, unsigned long end, unsigned long *pc, unsigned long *eaddr)
{
	return romvec->load(name, end, pc, eaddr);
}

long __init prom_invoke(unsigned long pc, unsigned long sp, long argc, char **argv, char **envp)
{
	return romvec->invoke(pc, sp, argc, argv, envp);
}

long __init prom_exec(char *name, long argc, char **argv, char **envp)
{
	return romvec->exec(name, argc, argv, envp);
}
