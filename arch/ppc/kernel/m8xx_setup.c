/*
 * $Id: m8xx_setup.c,v 1.4 1999/09/18 18:40:36 dmalek Exp $
 *
 *  linux/arch/ppc/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx by Dan.
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
#include <linux/bootmem.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>

#include <asm/time.h>
#include "ppc8xx_pic.h"

static int m8xx_set_rtc_time(unsigned long time);
unsigned long m8xx_get_rtc_time(void);
void m8xx_calibrate_decr(void);

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
extern void m8xx_cpm_reset(uint);

void __init adbdev_init(void)
{
}

void __init
m8xx_setup_arch(void)
{
	int	cpm_page;
	extern char cmd_line[];
	
	cpm_page = (int) alloc_bootmem_pages(PAGE_SIZE);
	
	printk("Boot arguments: %s\n", cmd_line);

	/* Reset the Communication Processor Module.
	*/
	m8xx_cpm_reset(cpm_page);

#ifdef notdef
	ROOT_DEV = to_kdev_t(0x0301); /* hda1 */
#endif
	
#ifdef CONFIG_BLK_DEV_INITRD
#if 0
	ROOT_DEV = to_kdev_t(0x0200); /* floppy */  
	rd_prompt = 1;
	rd_doload = 1;
	rd_image_start = 0;
#endif
#if 0	/* XXX this may need to be updated for the new bootmem stuff,
	   or possibly just deleted (see set_phys_avail() in init.c).
	   - paulus. */
	/* initrd_start and size are setup by boot/head.S and kernel/head.S */
	if ( initrd_start )
	{
		if (initrd_end > *memory_end_p)
		{
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,*memory_end_p);
			initrd_start = 0;
		}
	}
#endif
#endif
}

void
abort(void)
{
#ifdef CONFIG_XMON
	xmon(0);
#endif
	machine_restart(NULL);
}

/* A place holder for time base interrupts, if they are ever enabled.
*/
void timebase_interrupt(int irq, void * dev, struct pt_regs * regs)
{
	printk("timebase_interrupt()\n");
}

/* The decrementer counts at the system (internal) clock frequency divided by
 * sixteen, or external oscillator divided by four.  We force the processor
 * to use system clock divided by sixteen.
 */
void __init m8xx_calibrate_decr(void)
{
	bd_t	*binfo = (bd_t *)__res;
	int freq, fp, divisor;

	/* Unlock the SCCR.
	*/
	((volatile immap_t *)IMAP_ADDR)->im_clkrstk.cark_sccrk = ~KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_clkrstk.cark_sccrk = KAPWR_KEY;

	/* Force all 8xx processors to use divide by 16 processor clock.
	*/
	((volatile immap_t *)IMAP_ADDR)->im_clkrst.car_sccr |= 0x02000000;

	/* Processor frequency is MHz.
	 * The value 'fp' is the number of decrementer ticks per second.
	 */
	fp = (binfo->bi_intfreq * 1000000) / 16;
	freq = fp*60;	/* try to make freq/1e6 an integer */
        divisor = 60;
        printk("time_init: decrementer frequency = %d/%d\n", freq, divisor);
        tb_ticks_per_jiffy = freq / HZ / divisor;
	tb_to_us = mulhwu_scale_factor(freq / divisor, 1000000);

	/* Perform some more timer/timebase initialization.  This used
	 * to be done elsewhere, but other changes caused it to get
	 * called more than once....that is a bad thing.
	 *
	 * First, unlock all of the registers we are going to modify.
	 * To protect them from corruption during power down, registers
	 * that are maintained by keep alive power are "locked".  To
	 * modify these registers we have to write the key value to
	 * the key location associated with the register.
	 * Some boards power up with these unlocked, while others
	 * are locked.  Writing anything (including the unlock code?)
	 * to the unlocked registers will lock them again.  So, here
	 * we guarantee the registers are locked, then we unlock them
	 * for our use.
	 */
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_tbscrk = ~KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_rtcsck = ~KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_tbk = ~KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_tbscrk = KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_rtcsck = KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_tbk = KAPWR_KEY;

	/* Disable the RTC one second and alarm interrupts.
	*/
	((volatile immap_t *)IMAP_ADDR)->im_sit.sit_rtcsc &=
						~(RTCSC_SIE | RTCSC_ALE);

	/* Enabling the decrementer also enables the timebase interrupts
	 * (or from the other point of view, to get decrementer interrupts
	 * we have to enable the timebase).  The decrementer interrupt
	 * is wired into the vector table, nothing to do here for that.
	 */
	((volatile immap_t *)IMAP_ADDR)->im_sit.sit_tbscr =
				((mk_int_int_mask(DEC_INTERRUPT) << 8) |
					 (TBSCR_TBF | TBSCR_TBE));

	if (request_8xxirq(DEC_INTERRUPT, timebase_interrupt, 0, "tbint", NULL) != 0)
		panic("Could not allocate timer IRQ!");
}

/* The RTC on the MPC8xx is an internal register.
 * We want to protect this during power down, so we need to unlock,
 * modify, and re-lock.
 */
static int
m8xx_set_rtc_time(unsigned long time)
{
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck = KAPWR_KEY;
	((volatile immap_t *)IMAP_ADDR)->im_sit.sit_rtc = time;
	((volatile immap_t *)IMAP_ADDR)->im_sitk.sitk_rtck = ~KAPWR_KEY;
	return(0);
}

unsigned long __init
m8xx_get_rtc_time(void)
{
	/* Get time from the RTC.
	*/
	return((unsigned long)(((immap_t *)IMAP_ADDR)->im_sit.sit_rtc));
}

void
m8xx_restart(char *cmd)
{
	extern void m8xx_gorom(void);

	m8xx_gorom();
}

void
m8xx_power_off(void)
{
   m8xx_restart(NULL);
}

void
m8xx_halt(void)
{
   m8xx_restart(NULL);
}


int m8xx_setup_residual(char *buffer)
{
        int     len = 0;
	bd_t	*bp;

	bp = (bd_t *)__res;
			
	len += sprintf(len+buffer,"clock\t\t: %dMHz\n"
		       "bus clock\t: %dMHz\n",
		       bp->bi_intfreq /*/ 1000000*/,
		       bp->bi_busfreq /*/ 1000000*/);

	return len;
}

/* Initialize the internal interrupt controller.  The number of
 * interrupts supported can vary with the processor type, and the
 * 82xx family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
void __init
m8xx_init_IRQ(void)
{
	int i;
	void cpm_interrupt_init(void);

        for ( i = 0 ; i < NR_SIU_INTS ; i++ )
                irq_desc[i].handler = &ppc8xx_pic;
	
	/* We could probably incorporate the CPM into the multilevel
	 * interrupt structure.
	 */
	cpm_interrupt_init();
        unmask_irq(CPM_INTERRUPT);

#if defined(CONFIG_PCI)
        for ( i = NR_SIU_INTS ; i < (NR_SIU_INTS + NR_8259_INTS) ; i++ )
                irq_desc[i].handler = &i8259_pic;
        i8259_pic.irq_offset = NR_SIU_INTS;
        i8259_init();
        request_8xxirq(ISA_BRIDGE_INT, mbx_i8259_action, 0, "8259 cascade", NULL);
        enable_irq(ISA_BRIDGE_INT);
#endif
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)

/* Define this to make a PCMCIA ATA Flash card work.
*/
#define ATA_FLASH 1

/*
 * IDE stuff.
 */
void
m8xx_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
#ifdef ATA_FLASH
	ide_insw(port, buf, ns);
#else
	ide_insw(port+_IO_BASE, buf, ns);
#endif
}

void
m8xx_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
#ifdef ATA_FLASH
	ide_outsw(port, buf, ns);
#else
	ide_outsw(port+_IO_BASE, buf, ns);
#endif
}

int
m8xx_ide_default_irq(ide_ioreg_t base)
{
#ifdef ATA_FLASH
	return PCMCIA_INTERRUPT;
#else
        return 14;
#endif
}

ide_ioreg_t
m8xx_ide_default_io_base(int index)
{
        return index;
}

int
m8xx_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return 0;
}

void
m8xx_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
}

void
m8xx_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
}

int
m8xx_ide_request_irq(unsigned int irq,
		       void (*handler)(int, void *, struct pt_regs *),
		       unsigned long flags, 
		       const char *device,
		       void *dev_id)
{
#ifdef ATA_FLASH
	return request_8xxirq(irq, handler, flags, device, dev_id);
#else
	return request_irq(irq, handler, flags, device, dev_id);
#endif
}

void
m8xx_ide_fix_driveid(struct hd_driveid *id)
{
        ppc_generic_ide_fix_driveid(id);
}

/* We can use an external IDE controller or wire the IDE interface to
 * the internal PCMCIA controller.
 */
void __init m8xx_ide_init_hwif_ports(ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	ide_ioreg_t port = base;
	int i;
#ifdef ATA_FLASH
	volatile pcmconf8xx_t	*pcmp;
#endif

#ifdef ATA_FLASH
	*p = 0;
	*irq = 0;

	if (base != 0)		/* Only map the first ATA flash drive */
		return;

	pcmp = (pcmconf8xx_t *)(&(((immap_t *)IMAP_ADDR)->im_pcmcia));
	if (pcmp->pcmc_pipr & 0x18000000)
		return;		/* No card in slot */

	base = (unsigned long) ioremap(PCMCIA_MEM_ADDR, 0x200);

	/* For the M-Systems ATA card, the first 8 registers map 1:1.
	 * The following register, control/Altstatus, is located at 0x0e.
	 * Following that, the irq offset, is not used, so we place it in
	 * an unused location, 0x0a.
	 */
	*p++ = base + 8;
	for (i = 1; i < 8; ++i)
		*p++ = base + i;
	*p++ = base + 0x0e;		/* control/altstatus */
	*p = base + 0x0a;		/* IRQ, not used */
	if (irq)
		*irq = PCMCIA_INTERRUPT;

	/* Configure the interface for this interrupt.
	*/
	pcmp->pcmc_pgcra = (mk_int_int_mask(PCMCIA_INTERRUPT) << 24) |
		(mk_int_int_mask(PCMCIA_INTERRUPT) << 16);

	/* Enable status change interrupt from slot A.
	*/
	pcmp->pcmc_per = 0xff100000;
	pcmp->pcmc_pscr = ~0;
#else
	
	/* Just a regular IDE drive on some I/O port.
	*/
	i = 8;
	while (i--)
		*p++ = port++;
	*p++ = base + 0x206;
	if (irq != NULL)
		*irq = 0;
#endif
}
#endif

void __init
m8xx_init(unsigned long r3, unsigned long r4, unsigned long r5,
	 unsigned long r6, unsigned long r7)
{

	if ( r3 )
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
	
#ifdef CONFIG_PCI
	m8xx_setup_pci_ptrs();
#endif

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

	ppc_md.setup_arch     = m8xx_setup_arch;
	ppc_md.setup_residual = m8xx_setup_residual;
	ppc_md.get_cpuinfo    = NULL;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = m8xx_init_IRQ;
	ppc_md.get_irq	      = m8xx_get_irq;
	ppc_md.init           = NULL;

	ppc_md.restart        = m8xx_restart;
	ppc_md.power_off      = m8xx_power_off;
	ppc_md.halt           = m8xx_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = m8xx_set_rtc_time;
	ppc_md.get_rtc_time   = m8xx_get_rtc_time;
	ppc_md.calibrate_decr = m8xx_calibrate_decr;

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
