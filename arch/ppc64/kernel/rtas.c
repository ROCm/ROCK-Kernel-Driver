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
#include <linux/module.h>
#include <linux/init.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/semaphore.h>
#include <asm/machdep.h>
#include <asm/paca.h>
#include <asm/page.h>
#include <asm/param.h>
#include <asm/system.h>
#include <asm/abs_addr.h>
#include <asm/udbg.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

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

char rtas_err_buf[RTAS_ERROR_LOG_MAX];

extern unsigned long reloc_offset(void);

spinlock_t rtas_data_buf_lock = SPIN_LOCK_UNLOCKED;
char rtas_data_buf[RTAS_DATA_BUF_SIZE]__page_aligned;

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

int
rtas_token(const char *service)
{
	int *tokp;
	if (rtas.dev == NULL) {
		PPCDBG(PPCDBG_RTAS,"\tNo rtas device in device-tree...\n");
		return RTAS_UNKNOWN_SERVICE;
	}
	tokp = (int *) get_property(rtas.dev, service, NULL);
	return tokp ? *tokp : RTAS_UNKNOWN_SERVICE;
}

void
log_rtas_error(struct rtas_args	*rtas_args)
{
	struct rtas_args err_args, temp_args;

	err_args.token = rtas_token("rtas-last-error");
	err_args.nargs = 2;
	err_args.nret = 1;
	err_args.rets = (rtas_arg_t *)&(err_args.args[2]);

	err_args.args[0] = (rtas_arg_t)__pa(rtas_err_buf);
	err_args.args[1] = RTAS_ERROR_LOG_MAX;
	err_args.args[2] = 0;

	temp_args = *rtas_args;
	get_paca()->xRtas = err_args;

	PPCDBG(PPCDBG_RTAS, "\tentering rtas with 0x%lx\n",
	       (void *)__pa((unsigned long)&err_args));
	enter_rtas((void *)__pa((unsigned long)&get_paca()->xRtas));
	PPCDBG(PPCDBG_RTAS, "\treturned from rtas ...\n");


	err_args = get_paca()->xRtas;
	get_paca()->xRtas = temp_args;

	if (err_args.rets[0] == 0)
		log_error(rtas_err_buf, ERR_TYPE_RTAS_LOG, 0);
}

long
rtas_call(int token, int nargs, int nret,
	  unsigned long *outputs, ...)
{
	va_list list;
	int i;
	unsigned long s;
	struct rtas_args *rtas_args = &(get_paca()->xRtas);

	PPCDBG(PPCDBG_RTAS, "Entering rtas_call\n");
	PPCDBG(PPCDBG_RTAS, "\ttoken    = 0x%x\n", token);
	PPCDBG(PPCDBG_RTAS, "\tnargs    = %d\n", nargs);
	PPCDBG(PPCDBG_RTAS, "\tnret     = %d\n", nret);
	PPCDBG(PPCDBG_RTAS, "\t&outputs = 0x%lx\n", outputs);
	if (token == RTAS_UNKNOWN_SERVICE)
		return -1;

	rtas_args->token = token;
	rtas_args->nargs = nargs;
	rtas_args->nret  = nret;
	rtas_args->rets  = (rtas_arg_t *)&(rtas_args->args[nargs]);
	va_start(list, outputs);
	for (i = 0; i < nargs; ++i) {
		rtas_args->args[i] = (rtas_arg_t)LONG_LSW(va_arg(list, ulong));
		PPCDBG(PPCDBG_RTAS, "\tnarg[%d] = 0x%lx\n", i, rtas_args->args[i]);
	}
	va_end(list);

	for (i = 0; i < nret; ++i)
	  rtas_args->rets[i] = 0;

#if 0   /* Gotta do something different here, use global lock for now... */
	spin_lock_irqsave(&rtas_args->lock, s);
#else
	spin_lock_irqsave(&rtas.lock, s);
#endif
	PPCDBG(PPCDBG_RTAS, "\tentering rtas with 0x%lx\n",
		(void *)__pa((unsigned long)rtas_args));
	enter_rtas((void *)__pa((unsigned long)rtas_args));
	PPCDBG(PPCDBG_RTAS, "\treturned from rtas ...\n");

	if (rtas_args->rets[0] == -1)
		log_rtas_error(rtas_args);

#if 0   /* Gotta do something different here, use global lock for now... */
	spin_unlock_irqrestore(&rtas_args->lock, s);
#else
	spin_unlock_irqrestore(&rtas.lock, s);
#endif
	ifppcdebug(PPCDBG_RTAS) {
		for(i=0; i < nret ;i++)
			udbg_printf("\tnret[%d] = 0x%lx\n", i, (ulong)rtas_args->rets[i]);
	}

	if (nret > 1 && outputs != NULL)
		for (i = 0; i < nret-1; ++i)
			outputs[i] = rtas_args->rets[i+1];
	return (ulong)((nret > 0) ? rtas_args->rets[0] : 0);
}

/* Given an RTAS status code of 990n compute the hinted delay of 10^n
 * (last digit) milliseconds.  For now we bound at n=5 (100 sec).
 */
unsigned int
rtas_extended_busy_delay_time(int status)
{
	int order = status - 9900;
	unsigned long ms;

	if (order < 0)
		order = 0;	/* RTC depends on this for -2 clock busy */
	else if (order > 5)
		order = 5;	/* bound */

	/* Use microseconds for reasonable accuracy */
	for (ms=1; order > 0; order--)
		ms *= 10;          

	return ms; 
}

int
rtas_get_power_level(int powerdomain, int *level)
{
	int token = rtas_token("get-power-level");
	long powerlevel;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return RTAS_UNKNOWN_OP;

	while(1) {
		rc = (int) rtas_call(token, 1, 2, &powerlevel, powerdomain);
		if (rc == RTAS_BUSY)
			udelay(1);
		else
			break;
	}
	*level = (int) powerlevel;
	return rc;
}

int
rtas_set_power_level(int powerdomain, int level, int *setlevel)
{
	int token = rtas_token("set-power-level");
	unsigned int wait_time;
	long returned_level;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return RTAS_UNKNOWN_OP;

	while (1) {
		rc = (int) rtas_call(token, 2, 2, &returned_level, powerdomain,
					level);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		}
		else
			break;
	}
	*setlevel = (int) returned_level;
	return rc;
}

int
rtas_get_sensor(int sensor, int index, int *state)
{
	int token = rtas_token("get-sensor-state");
	unsigned int wait_time;
	long returned_state;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return RTAS_UNKNOWN_OP;

	while (1) {
		rc = (int) rtas_call(token, 2, 2, &returned_state, sensor,
					index);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		}
		else
			break;
	}
	*state = (int) returned_state;
	return rc;
}

int
rtas_set_indicator(int indicator, int index, int new_value)
{
	int token = rtas_token("set-indicator");
	unsigned int wait_time;
	int rc;

	if (token == RTAS_UNKNOWN_SERVICE)
		return RTAS_UNKNOWN_OP;

	while (1) {
		rc = (int) rtas_call(token, 3, 1, NULL, indicator, index,
					new_value);
		if (rc == RTAS_BUSY)
			udelay(1);
		else if (rtas_is_extended_busy(rc)) {
			wait_time = rtas_extended_busy_delay_time(rc);
			udelay(wait_time * 1000);
		}
		else
			break;
	}

	return rc;
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
	rtas_block_list = virt_to_abs(flist);
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
			f->blocks[i].data = (char *)virt_to_abs(f->blocks[i].data);
			image_size += f->blocks[i].length;
		}
		next = f->next;
		/* Don't translate NULL pointer for last entry */
		if (f->next)
			f->next = (struct flash_block_list *)virt_to_abs(f->next);
		else
			f->next = 0LL;
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

/* Must be in the RMO region, so we place it here */
static char rtas_os_term_buf[2048];

void rtas_os_term(char *str)
{
	long status;

	snprintf(rtas_os_term_buf, 2048, "OS panic: %s", str);

	do {
		status = rtas_call(rtas_token("ibm,os-term"), 1, 1, NULL,
				   __pa(rtas_os_term_buf));

		if (status == RTAS_BUSY)
			udelay(1);
		else if (status != 0)
			printk(KERN_EMERG "ibm,os-term call failed %ld\n",
			       status);
	} while (status == RTAS_BUSY);
}

unsigned long rtas_rmo_buf = 0;

asmlinkage int ppc_rtas(struct rtas_args __user *uargs)
{
	struct rtas_args args;
	unsigned long flags;
	int nargs;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&args, uargs, 3 * sizeof(u32)) != 0)
		return -EFAULT;

	nargs = args.nargs;
	if (nargs > ARRAY_SIZE(args.args)
	    || args.nret > ARRAY_SIZE(args.args)
	    || nargs + args.nret > ARRAY_SIZE(args.args))
		return -EINVAL;

	/* Copy in args. */
	if (copy_from_user(args.args, uargs->args,
			   nargs * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	spin_lock_irqsave(&rtas.lock, flags);

	get_paca()->xRtas = args;
	enter_rtas((void *)__pa((unsigned long)&get_paca()->xRtas));
	args = get_paca()->xRtas;

	args.rets  = (rtas_arg_t *)&(args.args[nargs]);
	if (args.rets[0] == -1)
		log_rtas_error(&args);

	spin_unlock_irqrestore(&rtas.lock, flags);

	/* Copy out args. */
	if (copy_to_user(uargs->args + nargs,
			 args.args + nargs,
			 args.nret * sizeof(rtas_arg_t)) != 0)
		return -EFAULT;

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
/* This version can't take the spinlock. */

void rtas_stop_self(void)
{
	struct rtas_args *rtas_args = &(get_paca()->xRtas);

	rtas_args->token = rtas_token("stop-self");
	BUG_ON(rtas_args->token == RTAS_UNKNOWN_SERVICE);
	rtas_args->nargs = 0;
	rtas_args->nret  = 1;
	rtas_args->rets  = &(rtas_args->args[0]);

	printk("%u %u Ready to die...\n",
	       smp_processor_id(), hard_smp_processor_id());
	enter_rtas((void *)__pa(rtas_args));
	panic("Alas, I survived.\n");
}
#endif /* CONFIG_HOTPLUG_CPU */

EXPORT_SYMBOL(rtas_firmware_flash_list);
EXPORT_SYMBOL(rtas_token);
EXPORT_SYMBOL(rtas_call);
EXPORT_SYMBOL(rtas_data_buf);
EXPORT_SYMBOL(rtas_data_buf_lock);
EXPORT_SYMBOL(rtas_extended_busy_delay_time);
EXPORT_SYMBOL(rtas_get_sensor);
EXPORT_SYMBOL(rtas_get_power_level);
EXPORT_SYMBOL(rtas_set_power_level);
EXPORT_SYMBOL(rtas_set_indicator);
