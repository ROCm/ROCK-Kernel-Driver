/*
 *
 * Procedures for interfacing to the RTAS on CHRP machines.
 *
 * Peter Bergner, IBM	March 2001.
 * Copyright (C) 2001 IBM.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#include <asm/init.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/udbg.h>

/*
 * prom_init() is called very early on, before the kernel text
 * and data have been mapped to KERNELBASE.  At this point the code
 * is running at whatever address it has been loaded at, so
 * references to extern and static variables must be relocated
 * explicitly.  The procedure reloc_offset() returns the address
 * we're currently running at minus the address we were linked at.
 * (Note that strings count as static variables.)
 *
 * Because OF may have mapped I/O devices into the area starting at
 * KERNELBASE, particularly on CHRP machines, we can't safely call
 * OF once the kernel has been mapped to KERNELBASE.  Therefore all
 * OF calls should be done within prom_init(), and prom_init()
 * and all routines called within it must be careful to relocate
 * references as necessary.
 *
 * Note that the bss is cleared *after* prom_init runs, so we have
 * to make sure that any static or extern variables it accesses
 * are put in the data segment.
 */

struct rtas_t rtas = { 
	lock: SPIN_LOCK_UNLOCKED
};

extern unsigned long reloc_offset(void);

void
phys_call_rtas(int token, int nargs, int nret, ...)
{
	va_list list;
	unsigned long offset = reloc_offset();
	struct rtas_args *rtas = PTRRELOC(&(get_paca()->xRtas));
	int i;

	rtas->token = token;
	rtas->nargs = nargs;
	rtas->nret  = nret;
	rtas->rets  = (rtas_arg_t *)PTRRELOC(&(rtas->args[nargs]));

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
	  rtas->args[i] = (rtas_arg_t)LONG_LSW(va_arg(list, ulong));
	va_end(list);

        enter_rtas(rtas);	
}

void
phys_call_rtas_display_status(char c)
{
	unsigned long offset = reloc_offset();
	struct rtas_args *rtas = PTRRELOC(&(get_paca()->xRtas));

	rtas->token = 10;
	rtas->nargs = 1;
	rtas->nret  = 1;
	rtas->rets  = (rtas_arg_t *)PTRRELOC(&(rtas->args[1]));
	rtas->args[0] = (int)c;

	enter_rtas(rtas);	
}

void
call_rtas_display_status(char c)
{
	struct rtas_args *rtas = &(get_paca()->xRtas);

	rtas->token = 10;
	rtas->nargs = 1;
	rtas->nret  = 1;
	rtas->rets  = (rtas_arg_t *)&(rtas->args[1]);
	rtas->args[0] = (int)c;

	enter_rtas((void *)__pa((unsigned long)rtas));	
}

#if 0
#define DEBUG_RTAS
#endif
__openfirmware
int
rtas_token(const char *service)
{
	int *tokp;
	if (rtas.dev == NULL) {
#ifdef DEBUG_RTAS
		udbg_printf("\tNo rtas device in device-tree...\n");
#endif /* DEBUG_RTAS */
		return RTAS_UNKNOWN_SERVICE;
	}
	tokp = (int *) get_property(rtas.dev, service, NULL);
	return tokp ? *tokp : RTAS_UNKNOWN_SERVICE;
}

__openfirmware
long
rtas_call(int token, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i;
	unsigned long s;
	struct rtas_args *rtas_args = &(get_paca()->xRtas);

#ifdef DEBUG_RTAS
	udbg_printf("Entering rtas_call\n");
	udbg_printf("\ttoken    = 0x%x\n", token);
	udbg_printf("\tnargs    = %d\n", nargs);
	udbg_printf("\tnret     = %d\n", nret);
	udbg_printf("\t&outputs = 0x%lx\n", outputs);
#endif /* DEBUG_RTAS */
	if (token == RTAS_UNKNOWN_SERVICE)
		return -1;

	rtas_args->token = token;
	rtas_args->nargs = nargs;
	rtas_args->nret  = nret;
	rtas_args->rets  = (rtas_arg_t *)&(rtas_args->args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i) {
	  rtas_args->args[i] = (rtas_arg_t)LONG_LSW(va_arg(list, ulong));
#ifdef DEBUG_RTAS
	  udbg_printf("\tnarg[%d] = 0x%lx\n", i, rtas_args->args[i]);
#endif /* DEBUG_RTAS */
	}
	va_end(list);

	for (i = 0; i < nret; ++i)
	  rtas_args->rets[i] = 0;

#if 0   /* Gotta do something different here, use global lock for now... */
	spin_lock_irqsave(&rtas_args->lock, s);
#else
	spin_lock_irqsave(&rtas.lock, s);
#endif
#ifdef DEBUG_RTAS
	udbg_printf("\tentering rtas with 0x%lx\n", (void *)__pa((unsigned long)rtas_args));
#endif /* DEBUG_RTAS */
	enter_rtas((void *)__pa((unsigned long)rtas_args));
#ifdef DEBUG_RTAS
	udbg_printf("\treturned from rtas ...\n");
#endif /* DEBUG_RTAS */
#if 0   /* Gotta do something different here, use global lock for now... */
	spin_unlock_irqrestore(&rtas_args->lock, s);
#else
	spin_unlock_irqrestore(&rtas.lock, s);
#endif
#ifdef DEBUG_RTAS
	for(i=0; i < nret ;i++)
	  udbg_printf("\tnret[%d] = 0x%lx\n", i, (ulong)rtas_args->rets[i]);
#endif /* DEBUG_RTAS */

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = rtas_args->rets[i+1];
	return (ulong)((nret > 0) ? rtas_args->rets[0] : 0);
}

void __chrp
rtas_restart(char *cmd)
{
        printk("RTAS system-reboot returned %ld\n",
	       rtas_call(rtas_token("system-reboot"), 0, 1, NULL));
        for (;;);
}

void __chrp
rtas_power_off(void)
{
        /* allow power on only with power button press */
        printk("RTAS power-off returned %ld\n",
               rtas_call(rtas_token("power-off"), 2, 1, NULL,0xffffffff,0xffffffff));
        for (;;);
}

void __chrp
rtas_halt(void)
{
        rtas_power_off();
}
