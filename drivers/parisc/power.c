/*
 * linux/arch/parisc/kernel/power.c
 * HP PARISC soft power switch support driver
 *
 * Copyright (c) 2001-2002 Helge Deller <deller@gmx.de>
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *
 *
 * 
 *  HINT:
 *  Support of the soft power switch button may be enabled or disabled at
 *  runtime through the "/proc/sys/kernel/power" procfs entry.
 */ 

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>

#include <asm/pdc.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/led.h>
#include <asm/uaccess.h>


#ifdef DEBUG
# define DPRINTK(x) printk x
#else
# define DPRINTK(x) do { } while (0)
#endif


/* filename in /proc which can be used to enable/disable the power switch */
#define SYSCTL_FILENAME		"sys/kernel/power"


#define DIAG_CODE(code)		(0x14000000 + ((code)<<5))

/* this will go to processor.h or any other place... */
/* taken from PCXL ERS page 82 */
#define MFCPU_X(rDiagReg, t_ch, t_th, code) \
	(DIAG_CODE(code) + ((rDiagReg)<<21) + ((t_ch)<<16) + ((t_th)<<0) )
	
#define MTCPU(dr, gr)		MFCPU_X(dr, gr,  0, 0x12)       /* move value of gr to dr[dr] */
#define MFCPU_C(dr, gr)		MFCPU_X(dr, gr,  0, 0x30)	/* for dr0 and dr8 only ! */
#define MFCPU_T(dr, gr)		MFCPU_X(dr,  0, gr, 0xa0)	/* all dr except dr0 and dr8 */
	
#define __getDIAG(dr) ( { 			\
        register unsigned long __res asm("r28");\
	 __asm__ __volatile__ (			\
		".word %1\n nop\n" : "=&r" (__res) : "i" (MFCPU_T(dr,28)) \
	);					\
	__res;					\
} )


static void deferred_poweroff(void *dummy)
{
	extern int cad_pid;	/* from kernel/sys.c */
	if (kill_proc(cad_pid, SIGINT, 1)) {
		/* just in case killing init process failed */
		machine_power_off();
	}
}

/*
 * This function gets called from interrupt context.
 * As it's called within an interrupt, it wouldn't sync if we don't
 * use schedule_work().
 */

static DECLARE_WORK(poweroff_work, deferred_poweroff, NULL);

static void poweroff(void)
{
	static int powering_off;

	if (powering_off)
		return;

	powering_off++;
	schedule_work(&poweroff_work);
}


/* local time-counter for shutdown */
static int shutdown_timer;

/* check, give feedback and start shutdown after one second */
static void process_shutdown(void)
{
	if (shutdown_timer == 0)
		DPRINTK((KERN_INFO "Shutdown requested...\n"));

	shutdown_timer++;
	
	/* wait until the button was pressed for 1 second */
	if (shutdown_timer == HZ) {
		static char msg[] = "Shutting down...";
		DPRINTK((KERN_INFO "%s\n", msg));
#ifdef CONFIG_CHASSIS_LCD_LED
		lcd_print(msg);
#endif
		poweroff();
	}
}


/* main power switch tasklet struct (scheduled from time.c) */
DECLARE_TASKLET_DISABLED(power_tasklet, NULL, 0);

/* soft power switch enabled/disabled */
#ifdef CONFIG_PROC_FS
static int pwrsw_enabled = 1;
#else
#define pwrsw_enabled (1)
#endif

/*
 * On gecko style machines (e.g. 712/xx and 715/xx) 
 * the power switch status is stored in Bit 0 ("the highest bit")
 * of CPU diagnose register 25.
 * 
 */
static void gecko_tasklet_func(unsigned long unused)
{
	if (!pwrsw_enabled)
		return;

	if (__getDIAG(25) & 0x80000000) {
		/* power switch button not pressed or released again */
		/* Warning: Some machines do never reset this DIAG flag! */
		shutdown_timer = 0;
	} else {
		process_shutdown();
	}
}



/*
 * Check the power switch status which is read from the
 * real I/O location at soft_power_reg.
 * Bit 31 ("the lowest bit) is the status of the power switch.
 */

static void polling_tasklet_func(unsigned long soft_power_reg)
{
        unsigned long current_status;
	
	if (!pwrsw_enabled)
		return;

	current_status = gsc_readl(soft_power_reg);
	if (current_status & 0x1) {
		/* power switch button not pressed */
		shutdown_timer = 0;
	} else {
		process_shutdown();
	}
}


/*
 * powerfail interruption handler (irq IRQ_FROM_REGION(CPU_IRQ_REGION)+2) 
 */
#if 0
static void powerfail_interrupt(int code, void *x, struct pt_regs *regs)
{
	printk(KERN_CRIT "POWERFAIL INTERRUPTION !\n");
	poweroff();
}
#endif




/* 
 * /proc filesystem support 
 */

#ifdef CONFIG_SYSCTL
static int power_proc_read(char *page, char **start, off_t off, int count, 
	int *eof, void *data)
{
	char *out = page;
	int len;

	out += sprintf(out, "Software power switch support: ");
	out += sprintf(out, pwrsw_enabled ? "enabled (1)" : "disabled (0)" );
	out += sprintf(out, "\n");

	len = out - page - off;
	if (len < count) {
		*eof = 1;
		if (len <= 0) return 0;
	} else {
		len = count;
	}
	*start = page + off;
	return len;
}

static int power_proc_write(struct file *file, const char *buf, 
	unsigned long count, void *data)
{
	char *cur, lbuf[count];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	memset(lbuf, 0, count);

	copy_from_user(lbuf, buf, count);
	cur = lbuf;

	/* skip initial spaces */
	while (*cur && isspace(*cur))
		cur++;

	switch (*cur) {
	case '0':	pwrsw_enabled = 0;
			break;
	case '1':	pwrsw_enabled = 1;
			break;
	default:	printk(KERN_CRIT "/proc/" SYSCTL_FILENAME 
					": Parse error: only '0' or '1' allowed!\n");
			return -EINVAL;
	} /* switch() */

	return count;
}

static struct proc_dir_entry *ent;

static void __init power_create_procfs(void)
{
	if (!power_tasklet.func)
		return;
	
	ent = create_proc_entry(SYSCTL_FILENAME, S_IFREG|S_IRUGO|S_IWUSR, 0);
	if (!ent) return;
	
	ent->nlink = 1;
	ent->read_proc = power_proc_read;
	ent->write_proc = power_proc_write;
	ent->owner = THIS_MODULE;
}

static void __exit power_remove_procfs(void)
{
	remove_proc_entry(SYSCTL_FILENAME, NULL);
}

#else
#define power_create_procfs()	do { } while (0)
#define power_remove_procfs()	do { } while (0)
#endif	/* CONFIG_SYSCTL */



/* parisc_panic_event() is called by the panic handler.
 * As soon as a panic occurs, our tasklets above will not be
 * executed any longer. This function then re-enables the 
 * soft-power switch and allows the user to switch off the system
 */
static int parisc_panic_event(struct notifier_block *this,
		unsigned long event, void *ptr)
{
	/* re-enable the soft-power switch */
	pdc_soft_power_button(0);
	return NOTIFY_DONE;
}

static struct notifier_block parisc_panic_block = {
	notifier_call: parisc_panic_event,
	priority: INT_MAX,
};


static int __init power_init(void)
{
	unsigned long ret;
	unsigned long soft_power_reg = 0;

#if 0
	request_irq( IRQ_FROM_REGION(CPU_IRQ_REGION)+2, &powerfail_interrupt,
		0, "powerfail", NULL);
#endif

	/* enable the soft power switch if possible */
	ret = pdc_soft_power_info(&soft_power_reg);
	if (ret == PDC_OK)
		ret = pdc_soft_power_button(1);
	if (ret != PDC_OK)
		soft_power_reg = -1UL;
	
	switch (soft_power_reg) {
	case 0:		printk(KERN_INFO "Gecko-style soft power switch enabled.\n");
			power_tasklet.func = gecko_tasklet_func;
			break;
			
	case -1UL:	printk(KERN_INFO "Soft power switch support not available.\n");
			return -ENODEV;
	
	default:	printk(KERN_INFO "Soft power switch enabled, polling @ 0x%08lx.\n",
				soft_power_reg);
			power_tasklet.data = soft_power_reg;
			power_tasklet.func = polling_tasklet_func;
	}

	/* Register a call for panic conditions. */
	notifier_chain_register(&panic_notifier_list, &parisc_panic_block);

	power_create_procfs();
	tasklet_enable(&power_tasklet);

	return 0;
}

static void __exit power_exit(void)
{
	if (!power_tasklet.func)
		return;

	tasklet_disable(&power_tasklet);
	notifier_chain_unregister(&panic_notifier_list, &parisc_panic_block);
	power_remove_procfs();
	power_tasklet.func = NULL;
	pdc_soft_power_button(0);
}

module_init(power_init);
module_exit(power_exit);


MODULE_AUTHOR("Helge Deller");
MODULE_DESCRIPTION("Soft power switch driver");
MODULE_LICENSE("Dual BSD/GPL");


EXPORT_NO_SYMBOLS;

