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

int platform_timer_setup(irqreturn_t (*timer_int)(int, void *, struct pt_regs *))
{
	outb(H8300_TIMER_COUNT_DATA,_8TCORA1);
	outb(0x00,_8TCSR1);
	request_irq(76,timer_int,0,"timer",0);
	outb(0x40|0x08|0x03,_8TCR1);
	return 0;
}

void platform_timer_eoi(void)
{
        __asm__("bclr #6,@0xffffb3:8");
}

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	*year = *mon = *day = *hour = *min = *sec = 0;
}
