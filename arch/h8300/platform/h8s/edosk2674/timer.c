/*
 *  linux/arch/h8300/platform/h8s/generic/timer.c
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 *  Platform depend Timer Handler
 *
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/timex.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/regs267x.h>

#define REGS(regs) __REGS(regs)
#define __REGS(regs) #regs

int __init platform_timer_setup(irqreturn_t (*timer_int)(int, void *, struct pt_regs *))
{
	unsigned char mstpcrl;
	mstpcrl = inb(MSTPCRL);                  /* Enable timer */
	mstpcrl &= ~0x01;
	outb(mstpcrl,MSTPCRL);
	outb(H8300_TIMER_COUNT_DATA,_8TCORA1);
	outb(0x00,_8TCSR1);
	request_irq(76,timer_int,0,"timer",0);
	outb(0x40|0x08|0x03,_8TCR1);
	return 0;
}

void platform_timer_eoi(void)
{
        __asm__("bclr #6,@" REGS(_8TCSR1) ":8");
}

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
/* FIXME! not RTC support */
	*year = *mon = *day = *hour = *min = *sec = 0;
}
