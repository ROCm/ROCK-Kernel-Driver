/*
 * arch/arm/mach-omap/time.c
 *
 * OMAP Timer Tick 
 *
 * Copyright (C) 2000 RidgeRun, Inc.
 * Author: Greg Lonnon <glonnon@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/delay.h>
#include <asm/system.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/leds.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/arch/clocks.h>

#ifndef __instrument
#define __instrument
#define __noinstrument __attribute__ ((no_instrument_function))
#endif

typedef struct {
	u32 cntl;     /* CNTL_TIMER, R/W */
	u32 load_tim; /* LOAD_TIM,   W */
	u32 read_tim; /* READ_TIM,   R */
} mputimer_regs_t;

#define mputimer_base(n) \
    ((volatile mputimer_regs_t*)IO_ADDRESS(OMAP_MPUTIMER_BASE + \
				 (n)*OMAP_MPUTIMER_OFFSET))

static inline unsigned long timer32k_read(int reg) {
	unsigned long val;
	val = omap_readw(reg + OMAP_32kHz_TIMER_BASE);
	return val;
}
static inline void timer32k_write(int reg,int val) {
	omap_writew(val, reg + OMAP_32kHz_TIMER_BASE);
}

/*
 * How long is the timer interval? 100 HZ, right...
 * IRQ rate = (TVR + 1) / 32768 seconds
 * TVR = 32768 * IRQ_RATE -1
 * IRQ_RATE =  1/100
 * TVR = 326
 */
#define TIMER32k_PERIOD 326
//#define TIMER32k_PERIOD 0x7ff

static inline void start_timer32k(void) {
	timer32k_write(TIMER32k_CR,
		       TIMER32k_TSS | TIMER32k_TRB |
		       TIMER32k_INT | TIMER32k_ARL);
}

#ifdef CONFIG_MACH_OMAP_PERSEUS2
/*
 * After programming PTV with 0 and setting the MPUTIM_CLOCK_ENABLE
 * (external clock enable)  bit, the timer count rate is 6.5 MHz (13
 * MHZ input/2). !! The divider by 2 is undocumented !!
 */
#define MPUTICKS_PER_SEC (13000000/2)
#else
/*
 * After programming PTV with 0, the timer count rate is 6 MHz.
 * WARNING! this must be an even number, or machinecycles_to_usecs
 * below will break.
 */
#define MPUTICKS_PER_SEC  (12000000/2)
#endif

static int mputimer_started[3] = {0,0,0};

static inline void __noinstrument start_mputimer(int n,
						 unsigned long load_val)
{
	volatile mputimer_regs_t* timer = mputimer_base(n);

	mputimer_started[n] = 0;
	timer->cntl = MPUTIM_CLOCK_ENABLE;
	udelay(1);

	timer->load_tim = load_val;
        udelay(1);
	timer->cntl = (MPUTIM_CLOCK_ENABLE | MPUTIM_AR | MPUTIM_ST);
	mputimer_started[n] = 1;
}

static inline unsigned long __noinstrument
read_mputimer(int n)
{
	volatile mputimer_regs_t* timer = mputimer_base(n);
	return (mputimer_started[n] ? timer->read_tim : 0);
}

void __noinstrument start_mputimer1(unsigned long load_val)
{
	start_mputimer(0, load_val);
}
void __noinstrument start_mputimer2(unsigned long load_val)
{
	start_mputimer(1, load_val);
}
void __noinstrument start_mputimer3(unsigned long load_val)
{
	start_mputimer(2, load_val);
}

unsigned long __noinstrument read_mputimer1(void)
{
	return read_mputimer(0);
}
unsigned long __noinstrument read_mputimer2(void)
{
	return read_mputimer(1);
}
unsigned long __noinstrument read_mputimer3(void)
{
	return read_mputimer(2);
}

unsigned long __noinstrument do_getmachinecycles(void)
{
	return 0 - read_mputimer(0);
}

unsigned long __noinstrument machinecycles_to_usecs(unsigned long mputicks)
{
	/* Round up to nearest usec */
	return ((mputicks * 1000) / (MPUTICKS_PER_SEC / 2 / 1000) + 1) >> 1;
}

/*
 * This marks the time of the last system timer interrupt
 * that was *processed by the ISR* (timer 2).
 */
static unsigned long systimer_mark;

static unsigned long omap_gettimeoffset(void)
{
	/* Return elapsed usecs since last system timer ISR */
	return machinecycles_to_usecs(do_getmachinecycles() - systimer_mark);
}

static irqreturn_t
omap_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long now, ilatency;

	/*
	 * Mark the time at which the timer interrupt ocurred using
	 * timer1. We need to remove interrupt latency, which we can
	 * retrieve from the current system timer2 counter. Both the
	 * offset timer1 and the system timer2 are counting at 6MHz,
	 * so we're ok.
	 */
	now = 0 - read_mputimer1();
	ilatency = MPUTICKS_PER_SEC / 100 - read_mputimer2();
	systimer_mark = now - ilatency;

	timer_tick(regs);

	return IRQ_HANDLED;
}

static struct irqaction omap_timer_irq = {
	.name		= "OMAP Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= omap_timer_interrupt
};

void __init omap_init_time(void)
{
	/* Since we don't call request_irq, we must init the structure */
	gettimeoffset = omap_gettimeoffset;

#ifdef OMAP1510_USE_32KHZ_TIMER
	timer32k_write(TIMER32k_CR, 0x0);
	timer32k_write(TIMER32k_TVR,TIMER32k_PERIOD);
	setup_irq(INT_OS_32kHz_TIMER, &omap_timer_irq);
	start_timer32k();
#else
	setup_irq(INT_TIMER2, &omap_timer_irq);
	start_mputimer2(MPUTICKS_PER_SEC / 100 - 1);
#endif
}

