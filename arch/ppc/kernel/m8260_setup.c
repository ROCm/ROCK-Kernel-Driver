/*
 * $Id: m8xx_setup.c,v 1.4 1999/09/18 18:40:36 dmalek Exp $
 *
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx and 8260 by Dan.
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/blk.h>
#include <linux/ioport.h>
#include <linux/ide.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/mpc8260.h>
#include <asm/immap_8260.h>
#include <asm/machdep.h>

#include <asm/time.h>
#include "ppc8260_pic.h"

static int m8260_set_rtc_time(unsigned long time);
unsigned long m8260_get_rtc_time(void);
void m8260_calibrate_decr(void);

#if 0
extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_pretranslate(unsigned char scancode, char raw_mode);
extern int mackbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void mackbd_init_hw(void);
#endif

extern unsigned long loops_per_sec;

unsigned char __res[sizeof(bd_t)];
unsigned long empty_zero_page[1024];

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern char saved_command_line[256];

extern unsigned long find_available_memory(void);
extern void m8260_cpm_reset(void);

void __init adbdev_init(void)
{
}

void __init
m8260_setup_arch(void)
{
	extern char cmd_line[];
	
	printk("Boot arguments: %s\n", cmd_line);

	/* Reset the Communication Processor Module.
	*/
	m8260_cpm_reset();
}

void
abort(void)
{
#ifdef CONFIG_XMON
	extern void xmon(void *);
	xmon(0);
#endif
	machine_restart(NULL);
}

/* The decrementer counts at the system (internal) clock frequency
 * divided by four.
 */
void __init m8260_calibrate_decr(void)
{
	bd_t	*binfo = (bd_t *)__res;
	int freq, divisor;

	freq = (binfo->bi_busfreq * 1000000);
        divisor = 4;
        tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);
}

/* The 8260 has an internal 1-second timer update register that
 * we should use for this purpose.
 */
static uint	rtc_time;
static int
m8260_set_rtc_time(unsigned long time)
{
	rtc_time = time;
	return(0);
}

unsigned long __init
m8260_get_rtc_time(void)
{

	/* Get time from the RTC.
	*/
	return((unsigned long)rtc_time);
}

void
m8260_restart(char *cmd)
{
	extern void m8260_gorom(bd_t *bi, uint addr);
	uint	startaddr;

	/* Most boot roms have a warmstart as the second instruction
	 * of the reset vector.  If that doesn't work for you, change this
	 * or the reboot program to send a proper address.
	 */
	startaddr = 0xff000104;

	if (cmd != NULL) {
		if (!strncmp(cmd, "startaddr=", 10))
			startaddr = simple_strtoul(&cmd[10], NULL, 0);
	}

	m8260_gorom((uint)__pa(__res), startaddr);
}

void
m8260_power_off(void)
{
   m8260_restart(NULL);
}

void
m8260_halt(void)
{
   m8260_restart(NULL);
}


int m8260_setup_residual(char *buffer)
{
        int     len = 0;
	bd_t	*bp;

	bp = (bd_t *)__res;
			
	len += sprintf(len+buffer,"core clock\t: %d MHz\n"
		       "CPM  clock\t: %d MHz\n"
		       "bus  clock\t: %d MHz\n",
		       bp->bi_intfreq /*/ 1000000*/,
		       bp->bi_cpmfreq /*/ 1000000*/,
		       bp->bi_busfreq /*/ 1000000*/);

	return len;
}

/* Initialize the internal interrupt controller.  The number of
 * interrupts supported can vary with the processor type, and the
 * 8260 family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
void __init
m8260_init_IRQ(void)
{
	int i;
	void cpm_interrupt_init(void);

#if 0
        ppc8260_pic.irq_offset = 0;
#endif
        for ( i = 0 ; i < NR_SIU_INTS ; i++ )
                irq_desc[i].handler = &ppc8260_pic;
	
	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	immr->im_intctl.ic_sicr = 0;
	immr->im_intctl.ic_siprr = 0x05309770;
	immr->im_intctl.ic_scprrh = 0x05309770;
	immr->im_intctl.ic_scprrl = 0x05309770;

}


void __init
m8260_init(unsigned long r3, unsigned long r4, unsigned long r5,
	 unsigned long r6, unsigned long r7)
{

	if ( r3 )
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
	
#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if ( r4 )
	{
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */
	/* take care of cmd line */
	if ( r6 )
	{
		
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

	ppc_md.setup_arch     = m8260_setup_arch;
	ppc_md.setup_residual = m8260_setup_residual;
	ppc_md.get_cpuinfo    = NULL;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = m8260_init_IRQ;
	ppc_md.get_irq	      = m8260_get_irq;
	ppc_md.init           = NULL;

	ppc_md.restart        = m8260_restart;
	ppc_md.power_off      = m8260_power_off;
	ppc_md.halt           = m8260_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = m8260_set_rtc_time;
	ppc_md.get_rtc_time   = m8260_get_rtc_time;
	ppc_md.calibrate_decr = m8260_calibrate_decr;

#if 0
	ppc_md.kbd_setkeycode    = pckbd_setkeycode;
	ppc_md.kbd_getkeycode    = pckbd_getkeycode;
	ppc_md.kbd_pretranslate  = pckbd_pretranslate;
	ppc_md.kbd_translate     = pckbd_translate;
	ppc_md.kbd_unexpected_up = pckbd_unexpected_up;
	ppc_md.kbd_leds          = pckbd_leds;
	ppc_md.kbd_init_hw       = pckbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = pckbd_sysrq_xlate;
#endif
#else
	ppc_md.kbd_setkeycode    = NULL;
	ppc_md.kbd_getkeycode    = NULL;
	ppc_md.kbd_translate     = NULL;
	ppc_md.kbd_unexpected_up = NULL;
	ppc_md.kbd_leds          = NULL;
	ppc_md.kbd_init_hw       = NULL;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = NULL;
#endif
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.insw = m8xx_ide_insw;
        ppc_ide_md.outsw = m8xx_ide_outsw;
        ppc_ide_md.default_irq = m8xx_ide_default_irq;
        ppc_ide_md.default_io_base = m8xx_ide_default_io_base;
        ppc_ide_md.check_region = m8xx_ide_check_region;
        ppc_ide_md.request_region = m8xx_ide_request_region;
        ppc_ide_md.release_region = m8xx_ide_release_region;
        ppc_ide_md.fix_driveid = m8xx_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = m8xx_ide_init_hwif_ports;
        ppc_ide_md.ide_request_irq = m8xx_ide_request_irq;

        ppc_ide_md.io_base = _IO_BASE;
#endif		
}

void
prom_init(uint r3, uint r4, uint r5, uint r6)
{
	/* Nothing to do now, but we are called immediatedly upon
	 * kernel start up with MMU disabled, so if there is
	 * anything we need to do......
	 */
}

/* Mainly for ksyms.
*/
int
request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flag, const char *naem, void *dev)
{
	panic("request IRQ\n");
}
