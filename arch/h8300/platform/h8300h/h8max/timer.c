/*
 *  linux/arch/h8300/platform/h8300h/h8max/timer.c
 *
 *  Yoshinori Sato <ysato@users.sourcefoge.jp>
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

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/timex.h>

#define TMR8CMA2 0x00ffff94
#define TMR8TCSR2 0x00ffff92
#define TMR8TCNT2 0x00ffff90
#define CMFA 6

int platform_timer_setup(void (*timer_int)(int, void *, struct pt_regs *))
{
	outb(CONFIG_CLK_FREQ*10/8192,TMR8CMA2);
	outb(0x00,TMR8TCSR2);
	request_irq(40,timer_int,0,"timer",0);
	outb(0x40|0x08|0x03,TMR8TCNT2);
}

void platform_timer_eoi(void)
{
	*(unsigned char *)TMR8TCSR2 &= ~(1 << CMFA);
}

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	*year = *mon = *day = *hour = *min = *sec = 0;
}
