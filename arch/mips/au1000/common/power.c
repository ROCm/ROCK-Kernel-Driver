/*
 * BRIEF MODULE DESCRIPTION
 *	Au1000 Power Management routines.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 *  Some of the routines are right out of init/main.c, whose
 *  copyrights apply here.
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED	  ``AS	IS'' AND   ANY	EXPRESS OR IMPLIED
 *  WARRANTIES,	  INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO	EVENT  SHALL   THE AUTHOR  BE	 LIABLE FOR ANY	  DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED	  TO, PROCUREMENT OF  SUBSTITUTE GOODS	OR SERVICES; LOSS OF
 *  USE, DATA,	OR PROFITS; OR	BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN	 CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/string.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/au1000.h>

#define DEBUG 1
#ifdef DEBUG
#  define DPRINTK(fmt, args...)	printk("%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

extern void au1k_wait(void);
static void calibrate_delay(void);

extern void set_au1000_speed(unsigned int new_freq);
extern unsigned int get_au1000_speed(void);
extern unsigned long get_au1000_uart_baud_base(void);
extern void set_au1000_uart_baud_base(unsigned long new_baud_base);
extern unsigned long save_local_and_disable(int controller);
extern void restore_local_and_enable(int controller, unsigned long mask);
extern void local_enable_irq(unsigned int irq_nr);

/* Quick acpi hack. This will have to change! */
#define	CTL_ACPI 9999
#define	ACPI_S1_SLP_TYP 19
#define	ACPI_SLEEP 21

#ifdef CONFIG_PM

static spinlock_t pm_lock = SPIN_LOCK_UNLOCKED;

unsigned long suspend_mode;

void wakeup_from_suspend(void)
{
	suspend_mode = 0;
}

int au_sleep(void)
{
	unsigned long wakeup, flags;
	spin_lock_irqsave(&pm_lock,flags);

	flush_cache_all();
	/* pin 6 is gpio */
	au_writel(au_readl(SYS_PINSTATERD) & ~(1 << 11), SYS_PINSTATERD);

	/* gpio 6 can cause a wake up event */
	wakeup = au_readl(SYS_WAKEMSK);
	wakeup &= ~(1 << 8);	/* turn off match20 wakeup */
	wakeup |= 1 << 6;	/* turn on gpio 6 wakeup   */
	au_writel(wakeup, SYS_WAKEMSK);

	au_writel(1, SYS_WAKESRC);	/* clear cause */
	au_writel(1, SYS_SLPPWR);	/* prepare to sleep */

	__asm__("la $4, 1f\n\t"
		"lui $5, 0xb190\n\t"
		"ori $5, 0x18\n\t"
		"sw $4, 0($5)\n\t"
		"li $4, 1\n\t"
		"lui $5, 0xb190\n\t"
		"ori $5, 0x7c\n\t"
		"sw $4, 0($5)\n\t" "sync\n\t" "1:\t\n\t" "nop\n\t");

	/* after a wakeup, the cpu vectors back to 0x1fc00000 so
	 * it's up to the boot code to get us back here.
	 */
	spin_unlock_irqrestore(&pm_lock, flags);
	return 0;
}

static int pm_do_sleep(ctl_table * ctl, int write, struct file *file,
		       void *buffer, size_t * len)
{
	int retval = 0;

	if (!write) {
		*len = 0;
	} else {
		retval = pm_send_all(PM_SUSPEND, (void *) 2);
		if (retval)
			return retval;

		au_sleep();
		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}

static int pm_do_suspend(ctl_table * ctl, int write, struct file *file,
			 void *buffer, size_t * len)
{
	int retval = 0;

	if (!write) {
		*len = 0;
	} else {
		retval = pm_send_all(PM_SUSPEND, (void *) 2);
		if (retval)
			return retval;
		suspend_mode = 1;
		au1k_wait();
		retval = pm_send_all(PM_RESUME, (void *) 0);
	}
	return retval;
}


static int pm_do_freq(ctl_table * ctl, int write, struct file *file,
		      void *buffer, size_t * len)
{
	int retval = 0, i;
	unsigned long val, pll;
#define TMPBUFLEN 64
#define MAX_CPU_FREQ 396
	char buf[8], *p;
	unsigned long flags, intc0_mask, intc1_mask;
	unsigned long old_baud_base, old_cpu_freq, baud_rate, old_clk,
	    old_refresh;
	unsigned long new_baud_base, new_cpu_freq, new_clk, new_refresh;

	spin_lock_irqsave(&pm_lock, flags);
	if (!write) {
		*len = 0;
	} else {
		/* Parse the new frequency */
		if (*len > TMPBUFLEN - 1) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}
		if (copy_from_user(buf, buffer, *len)) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}
		buf[*len] = 0;
		p = buf;
		val = simple_strtoul(p, &p, 0);
		if (val > MAX_CPU_FREQ) {
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}

		pll = val / 12;
		if ((pll > 33) || (pll < 7)) {	/* 396 MHz max, 84 MHz min */
			/* revisit this for higher speed cpus */
			spin_unlock_irqrestore(&pm_lock, flags);
			return -EFAULT;
		}

		old_baud_base = get_au1000_uart_baud_base();
		old_cpu_freq = get_au1000_speed();

		new_cpu_freq = pll * 12 * 1000000;
		new_baud_base = (new_cpu_freq / 4) / 16;
		set_au1000_speed(new_cpu_freq);
		set_au1000_uart_baud_base(new_baud_base);

		old_refresh = au_readl(MEM_SDREFCFG) & 0x1ffffff;
		new_refresh =
		    ((old_refresh * new_cpu_freq) /
		     old_cpu_freq) | (au_readl(MEM_SDREFCFG) & ~0x1ffffff);

		au_writel(pll, SYS_CPUPLL);
		au_sync_delay(1);
		au_writel(new_refresh, MEM_SDREFCFG);
		au_sync_delay(1);

		for (i = 0; i < 4; i++) {
			if (au_readl
			    (UART_BASE + UART_MOD_CNTRL +
			     i * 0x00100000) == 3) {
				old_clk =
				    au_readl(UART_BASE + UART_CLK +
					  i * 0x00100000);
				// baud_rate = baud_base/clk
				baud_rate = old_baud_base / old_clk;
				/* we won't get an exact baud rate and the error
				 * could be significant enough that our new
				 * calculation will result in a clock that will
				 * give us a baud rate that's too far off from
				 * what we really want.
				 */
				if (baud_rate > 100000)
					baud_rate = 115200;
				else if (baud_rate > 50000)
					baud_rate = 57600;
				else if (baud_rate > 30000)
					baud_rate = 38400;
				else if (baud_rate > 17000)
					baud_rate = 19200;
				else
					(baud_rate = 9600);
				// new_clk = new_baud_base/baud_rate
				new_clk = new_baud_base / baud_rate;
				au_writel(new_clk,
				       UART_BASE + UART_CLK +
				       i * 0x00100000);
				au_sync_delay(10);
			}
		}
	}


	/* We don't want _any_ interrupts other than
	 * match20. Otherwise our calibrate_delay()
	 * calculation will be off, potentially a lot.
	 */
	intc0_mask = save_local_and_disable(0);
	intc1_mask = save_local_and_disable(1);
	local_enable_irq(AU1000_TOY_MATCH2_INT);
	spin_unlock_irqrestore(&pm_lock, flags);
	calibrate_delay();
	restore_local_and_enable(0, intc0_mask);
	restore_local_and_enable(1, intc1_mask);
	return retval;
}


static struct ctl_table pm_table[] = {
	{ACPI_S1_SLP_TYP, "suspend", NULL, 0, 0600, NULL, &pm_do_suspend},
	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &pm_do_sleep},
	{CTL_ACPI, "freq", NULL, 0, 0600, NULL, &pm_do_freq},
	{0}
};

static struct ctl_table pm_dir_table[] = {
	{CTL_ACPI, "pm", NULL, 0, 0555, pm_table},
	{0}
};

/*
 * Initialize power interface
 */
static int __init pm_init(void)
{
	register_sysctl_table(pm_dir_table, 1);
	return 0;
}

__initcall(pm_init);


/*
 * This is right out of init/main.c
 */

/* This is the number of bits of precision for the loops_per_jiffy.  Each
   bit takes on average 1.5/HZ seconds.  This (like the original) is a little
   better than 1% */
#define LPS_PREC 8

static void calibrate_delay(void)
{
	unsigned long ticks, loopbit;
	int lps_precision = LPS_PREC;

	loops_per_jiffy = (1 << 12);

	while (loops_per_jiffy <<= 1) {
		/* wait for "start of" clock tick */
		ticks = jiffies;
		while (ticks == jiffies)
			/* nothing */ ;
		/* Go .. */
		ticks = jiffies;
		__delay(loops_per_jiffy);
		ticks = jiffies - ticks;
		if (ticks)
			break;
	}

/* Do a binary approximation to get loops_per_jiffy set to equal one clock
   (up to lps_precision bits) */
	loops_per_jiffy >>= 1;
	loopbit = loops_per_jiffy;
	while (lps_precision-- && (loopbit >>= 1)) {
		loops_per_jiffy |= loopbit;
		ticks = jiffies;
		while (ticks == jiffies);
		ticks = jiffies;
		__delay(loops_per_jiffy);
		if (jiffies != ticks)	/* longer than 1 tick */
			loops_per_jiffy &= ~loopbit;
	}
}

void au1k_wait(void)
{
	__asm__("nop\n\t" "nop\n\t");
}

#endif				/* CONFIG_PM */
