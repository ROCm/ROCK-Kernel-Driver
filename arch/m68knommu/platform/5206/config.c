/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5206/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 * 	Copyright (C) 2000-2001, Lineo Inc. (www.lineo.com) 
 */

/***************************************************************************/

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int   dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
        MCF_MBAR + MCFDMA_BASE0,
        MCF_MBAR + MCFDMA_BASE1,
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void coldfire_tick(void)
{
	volatile unsigned char	*timerp;

	/* Reset the ColdFire timer */
	timerp = (volatile unsigned char *) (MCF_MBAR + MCFTIMER_BASE1);
	timerp[MCFTIMER_TER] = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;
}

/***************************************************************************/

void coldfire_timer_init(void (*handler)(int, void *, struct pt_regs *))
{
	volatile unsigned short	*timerp;
	volatile unsigned char	*icrp;

	/* Set up TIMER 1 as poll clock */
	timerp = (volatile unsigned short *) (MCF_MBAR + MCFTIMER_BASE1);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_DISABLE;

	timerp[MCFTIMER_TRR] = (unsigned short) ((MCF_CLK / 16) / HZ);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	icrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_TIMER1ICR);

	*icrp = MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL5 | MCFSIM_ICR_PRI3;
	request_irq(29, handler, SA_INTERRUPT, "ColdFire Timer", NULL);
	mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_TIMER1);
}

/***************************************************************************/

/*
 *	Program the vector to be an auto-vectored.
 */

void mcf_autovector(unsigned int vec)
{
	volatile unsigned char  *mbar;
	unsigned char		icr;

	if ((vec >= 25) && (vec <= 31)) {
		vec -= 25;
		mbar = (volatile unsigned char *) MCF_MBAR;
		icr = MCFSIM_ICR_AUTOVEC | (vec << 3);
		*(mbar + MCFSIM_ICR1 + vec) = icr;
		vec = 0x1 << (vec + 1);
		mcf_setimr(mcf_getimr() & ~vec);
	}
}

/***************************************************************************/

extern e_vector	*_ramvec;

void set_evector(int vecnum, void (*handler)(void))
{
	if (vecnum >= 0 && vecnum <= 255)
		_ramvec[vecnum] = handler;
}

/***************************************************************************/

/* assembler routines */
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void system_call(void);
asmlinkage void inthandler(void);

void coldfire_trap_init(void)
{
	int i;

#ifndef ENABLE_dBUG
	mcf_setimr(MCFSIM_IMR_MASKALL);
#endif

	/*
	 *	There is a common trap handler and common interrupt
	 *	handler that handle almost every vector. We treat
	 *	the system call and bus error special, they get their
	 *	own first level handlers.
	 */
#ifndef ENABLE_dBUG
	for (i = 3; (i <= 23); i++)
		_ramvec[i] = trap;
	for (i = 33; (i <= 63); i++)
		_ramvec[i] = trap;
#endif

	for (i = 24; (i <= 30); i++)
		_ramvec[i] = inthandler;
#ifndef ENABLE_dBUG
	_ramvec[31] = inthandler;  // Disables the IRQ7 button
#endif

	for (i = 64; (i < 255); i++)
		_ramvec[i] = inthandler;
	_ramvec[255] = 0;

	_ramvec[2] = buserr;
	_ramvec[32] = system_call;
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
	memset(commandp, 0, size);
	mach_sched_init = coldfire_timer_init;
	mach_tick = coldfire_tick;
	mach_trap_init = coldfire_trap_init;
}

/***************************************************************************/
