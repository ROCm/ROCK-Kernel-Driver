/*
 *  linux/arch/h8300/platform/h8300h/generic/timer.c
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

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/timex.h>

extern int request_irq_boot(unsigned int,
		             irqreturn_t (*handler)(int, void *, struct pt_regs *),
		             unsigned long, const char *, void *);


#if defined(CONFIG_H83007) || defined(CONFIG_H83068)
#include <asm/regs306x.h>

int platform_timer_setup(void (*timer_int)(int, void *, struct pt_regs *))
{
	outb(H8300_TIMER_COUNT_DATA,TMR8CMA2);
	outb(0x00,TMR8TCSR2);
	request_irq_boot(40,timer_int,0,"timer",0);
	outb(0x40|0x08|0x03,TMR8TCNT2);
	return 0;
}

void platform_timer_eoi(void)
{
	*(volatile unsigned char *)_8TCSR2 &= ~(1 << CMFA);
}
#endif

#if defined(H8_3002) || defined(CONFIG_H83048)
/* FIXME! */
#define TSTR 0x00ffff60
#define TSNC 0x00ffff61
#define TMDR 0x00ffff62
#define TFCR 0x00ffff63
#define TOER 0x00ffff90
#define TOCR 0x00ffff91
#define TCR  0x00ffff64
#define TIOR 0x00ffff65
#define TIER 0x00ffff66
#define TSR  0x00ffff67
#define TCNT 0x00ffff68
#define GRA  0x00ffff6a
#define GRB  0x00ffff6c

int platform_timer_setup(void (*timer_int)(int, void *, struct pt_regs *))
{
	*(unsigned short *)GRA= H8300_TIMER_COUNT_DATA;
	*(unsigned short *)TCNT=0;
	outb(0x23,TCR);
	outb(0x00,TIOR);
	request_timer_irq(26,timer_int,0,"timer",0);
	outb(inb(TIER) | 0x01,TIER);
	outb(inb(TSNC) & ~0x01,TSNC);
	outb(inb(TMDR) & ~0x01,TMDR);
	outb(inb(TSTR) | 0x01,TSTR);
	return 0;
}

void platform_timer_eoi(void)
{
	outb(inb(TSR) & ~0x01,TSR);
}
#endif

void platform_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	*year = *mon = *day = *hour = *min = *sec = 0;
}
