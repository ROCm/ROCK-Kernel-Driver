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

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/semaphore.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/abs_addr.h>
#include <asm/udbg.h>

struct proc_dir_entry *rtas_proc_dir;	/* /proc/ppc64/rtas dir */
struct flash_block_list_header rtas_firmware_flash_list = {0, 0};

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
	.lock = SPIN_LOCK_UNLOCKED
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

#define FLASH_BLOCK_LIST_VERSION (1UL)
static void
rtas_flash_firmware(void)
{
	unsigned long image_size;
	struct flash_block_list *f, *next, *flist;
	unsigned long rtas_block_list;
	int i, status, update_token;

	update_token = rtas_token("ibm,update-flash-64-and-reboot");
	if (update_token == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_ALERT "FLASH: ibm,update-flash-64-and-reboot is not available -- not a service partition?\n");
		printk(KERN_ALERT "FLASH: firmware will not be flashed\n");
		return;
	}

	/* NOTE: the "first" block list is a global var with no data
	 * blocks in the kernel data segment.  We do this because
	 * we want to ensure this block_list addr is under 4GB.
	 */
	rtas_firmware_flash_list.num_blocks = 0;
	flist = (struct flash_block_list *)&rtas_firmware_flash_list;
	rtas_block_list = virt_to_absolute((unsigned long)flist);
	if (rtas_block_list >= (4UL << 20)) {
		printk(KERN_ALERT "FLASH: kernel bug...flash list header addr above 4GB\n");
		return;
	}

	printk(KERN_ALERT "FLASH: preparing saved firmware image for flash\n");
	/* Update the block_list in place. */
	image_size = 0;
	for (f = flist; f; f = next) {
		/* Translate data addrs to absolute */
		for (i = 0; i < f->num_blocks; i++) {
			f->blocks[i].data = (char *)virt_to_absolute((unsigned long)f->blocks[i].data);
			image_size += f->blocks[i].length;
		}
		next = f->next;
		f->next = (struct flash_block_list *)virt_to_absolute((unsigned long)f->next);
		/* make num_blocks into the version/length field */
		f->num_blocks = (FLASH_BLOCK_LIST_VERSION << 56) | ((f->num_blocks+1)*16);
	}

	printk(KERN_ALERT "FLASH: flash image is %ld bytes\n", image_size);
	printk(KERN_ALERT "FLASH: performing flash and reboot\n");
	ppc_md.progress("Flashing        \n", 0x0);
	ppc_md.progress("Please Wait...  ", 0x0);
	printk(KERN_ALERT "FLASH: this will take several minutes.  Do not power off!\n");
	status = rtas_call(update_token, 1, 1, NULL, rtas_block_list);
	switch (status) {	/* should only get "bad" status */
	    case 0:
		printk(KERN_ALERT "FLASH: success\n");
		break;
	    case -1:
		printk(KERN_ALERT "FLASH: hardware error.  Firmware may not be not flashed\n");
		break;
	    case -3:
		printk(KERN_ALERT "FLASH: image is corrupt or not correct for this platform.  Firmware not flashed\n");
		break;
	    case -4:
		printk(KERN_ALERT "FLASH: flash failed when partially complete.  System may not reboot\n");
		break;
	    default:
		printk(KERN_ALERT "FLASH: unknown flash return code %d\n", status);
		break;
	}
}

void rtas_flash_bypass_warning(void)
{
	printk(KERN_ALERT "FLASH: firmware flash requires a reboot\n");
	printk(KERN_ALERT "FLASH: the firmware image will NOT be flashed\n");
}


void
rtas_restart(char *cmd)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_firmware();

        printk("RTAS system-reboot returned %ld\n",
	       rtas_call(rtas_token("system-reboot"), 0, 1, NULL));
        for (;;);
}

void
rtas_power_off(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
        /* allow power on only with power button press */
        printk("RTAS power-off returned %ld\n",
               rtas_call(rtas_token("power-off"), 2, 1, NULL,0xffffffff,0xffffffff));
        for (;;);
}

void
rtas_halt(void)
{
	if (rtas_firmware_flash_list.next)
		rtas_flash_bypass_warning();
        rtas_power_off();
}
