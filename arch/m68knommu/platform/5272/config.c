/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5272/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2002, SnapGear Inc. (www.snapgear.com)
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
#include <asm/delay.h>

/***************************************************************************/

void	reset_setupbutton(void);

/***************************************************************************/

/*
 *	DMA channel base address table.
 */
unsigned int   dma_base_addr[MAX_M68K_DMA_CHANNELS] = {
        MCF_MBAR + MCFDMA_BASE0,
};

unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

/***************************************************************************/

void coldfire_tick(void)
{
	volatile unsigned char	*timerp;

	/* Reset the ColdFire timer */
	timerp = (volatile unsigned char *) (MCF_MBAR + MCFTIMER_BASE4);
	timerp[MCFTIMER_TER] = MCFTIMER_TER_CAP | MCFTIMER_TER_REF;
}

/***************************************************************************/

void coldfire_timer_init(void (*handler)(int, void *, struct pt_regs *))
{
	volatile unsigned short	*timerp;
	volatile unsigned long	*icrp;

	/* Set up TIMER 4 as poll clock */
	timerp = (volatile unsigned short *) (MCF_MBAR + MCFTIMER_BASE4);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_DISABLE;

	timerp[MCFTIMER_TRR] = (unsigned short) ((MCF_CLK / 16) / HZ);
	timerp[MCFTIMER_TMR] = MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE;

	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = 0x0000000d; /* TMR4 with priority 5 */
	request_irq(72, handler, SA_INTERRUPT, "ColdFire Timer", NULL);

#ifdef CONFIG_RESETSWITCH
	/* This is not really the right place to do this... */
	reset_setupbutton();
#endif
}

/***************************************************************************/

/*
 *	Program the vector to be an auto-vectored.
 */

void mcf_autovector(unsigned int vec)
{
	/* Everything is auto-vectored on the 5272 */
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
	volatile unsigned long	*icrp;

	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	icrp[0] = 0x88888888;
	icrp[1] = 0x88888888;
	icrp[2] = 0x88888888;
	icrp[3] = 0x88888888;
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

void coldfire_reset(void)
{
	HARD_RESET_NOW();
}

/***************************************************************************/

void config_BSP(char *commandp, int size)
{
#if 0
	volatile unsigned long	*pivrp;

	/* Set base of device vectors to be 64 */
	pivrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_PIVR);
	*pivrp = 0x40;
#endif

#if defined(CONFIG_NETtel) || defined(CONFIG_eLIA) || \
    defined(CONFIG_DISKtel) || defined(CONFIG_SECUREEDGEMP3)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_MTD_KeyTechnology)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xffe06000, size);
	commandp[size-1] = 0;
#else
	memset(commandp, 0, size);
#endif

	mach_sched_init = coldfire_timer_init;
	mach_tick = coldfire_tick;
	mach_trap_init = coldfire_trap_init;
	mach_reset = coldfire_reset;
}

/***************************************************************************/
#ifdef CONFIG_RESETSWITCH
/***************************************************************************/

/*
 *	Routines to support the NETtel software reset button.
 */
void reset_button(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile unsigned long	*icrp, *isrp;
	extern void		flash_eraseconfig(void);
	static int		inbutton = 0;

	/*
	 *	IRQ7 is not maskable by the CPU core. It is possible
	 *	that switch bounce mey get us back here before we have
	 *	really serviced the interrupt.
	 */
	if (inbutton)
		return;
	inbutton = 1;

	/* Disable interrupt at SIM - best we can do... */
	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = (*icrp & 0x07777777) | 0x80000000;

	/* Try and de-bounce the switch a little... */
	udelay(10000);

	flash_eraseconfig();

	/* Don't leave here 'till button is no longer pushed! */
	isrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ISR);
	for (;;) {
		if (*isrp & 0x80000000)
			break;
	}

	HARD_RESET_NOW();
	/* Should never get here... */

	inbutton = 0;
	/* Interrupt service done, acknowledge it */
	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = (*icrp & 0x07777777) | 0xf0000000;
}

/***************************************************************************/

void reset_setupbutton(void)
{
	volatile unsigned long	*icrp;

	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	*icrp = (*icrp & 0x07777777) | 0xf0000000;
	request_irq(65, reset_button, (SA_INTERRUPT | IRQ_FLG_FAST),
		"Reset Button", NULL);
}

/***************************************************************************/
#endif /* CONFIG_RESETSWITCH */
/***************************************************************************/
